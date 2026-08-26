#!/usr/bin/env bash
# Validate and launch one budget-bounded x86 EC2 search-matrix instance.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${SCRIPT_DIR}/../../../.." && pwd)"
# shellcheck source=aws-run-cleanup.sh
source "${SCRIPT_DIR}/aws-run-cleanup.sh"

usage() {
  echo "usage: $0 [--plan] runs/<run-id>/ec2-run.env" >&2
}

PLAN=0
if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
  usage
  exit 0
fi
if [[ "${1:-}" == "--plan" ]]; then
  PLAN=1
  shift
fi
if [[ $# -ne 1 ]]; then
  usage
  exit 2
fi
EC2_CONFIG="$1"
if [[ ! -f "${EC2_CONFIG}" ]]; then
  echo "EC2 config not found: ${EC2_CONFIG}" >&2
  exit 2
fi
CONFIG_DIR="$(cd "$(dirname "${EC2_CONFIG}")" && pwd)"
if [[ "${CONFIG_DIR}" != "${ROOT}/runs/"* ]]; then
  echo "EC2 config must be inside ${ROOT}/runs" >&2
  exit 2
fi
if ! command -v aws >/dev/null 2>&1; then
  echo "AWS CLI is required" >&2
  exit 2
fi

# shellcheck disable=SC1090
source "${EC2_CONFIG}"
: "${AWS_REGION:?AWS_REGION is required}"
: "${AWS_AVAILABILITY_ZONE:?AWS_AVAILABILITY_ZONE is required}"
: "${AWS_INSTANCE_TYPE:?AWS_INSTANCE_TYPE is required}"
: "${AWS_SUBNET_ID:?AWS_SUBNET_ID is required}"
: "${AWS_SECURITY_GROUP_ID:?AWS_SECURITY_GROUP_ID is required}"
: "${AWS_IAM_INSTANCE_PROFILE:?AWS_IAM_INSTANCE_PROFILE is required}"
: "${AWS_SOURCE_S3_URI:?AWS_SOURCE_S3_URI is required}"
: "${AWS_SOURCE_SHA256:?AWS_SOURCE_SHA256 is required}"
: "${AWS_RESULT_S3_PREFIX:?AWS_RESULT_S3_PREFIX is required}"
: "${MATRIX_CONFIG_PATH:?MATRIX_CONFIG_PATH is required}"
: "${RUN_ID:?RUN_ID is required}"
: "${MAX_WALL_SECONDS:?MAX_WALL_SECONDS is required}"
: "${ASSERTED_HOURLY_PRICE_USD:?ASSERTED_HOURLY_PRICE_USD is required}"
: "${MAX_INSTANCE_COST_USD:?MAX_INSTANCE_COST_USD is required}"
: "${MIN_VCPUS:?MIN_VCPUS is required}"
: "${MIN_PHYSICAL_CORES:?MIN_PHYSICAL_CORES is required}"
: "${AWS_VCPU_QUOTA_NAME:?AWS_VCPU_QUOTA_NAME is required}"
: "${ROOT_VOLUME_GIB:?ROOT_VOLUME_GIB is required}"
: "${USE_CAPACITY_RESERVATION:?USE_CAPACITY_RESERVATION is required}"

for value in AWS_REGION AWS_AVAILABILITY_ZONE AWS_INSTANCE_TYPE AWS_SUBNET_ID AWS_SECURITY_GROUP_ID AWS_IAM_INSTANCE_PROFILE RUN_ID; do
  if [[ ! "${!value}" =~ ^[A-Za-z0-9._:/-]+$ ]]; then
    echo "unsafe characters in ${value}" >&2
    exit 2
  fi
done
if [[ ! "${AWS_VCPU_QUOTA_NAME}" =~ ^[A-Za-z0-9,()+\&/._[:space:]-]+$ ]]; then
  echo "AWS_VCPU_QUOTA_NAME contains unsafe characters" >&2
  exit 2
fi
if [[ ! "${MATRIX_CONFIG_PATH}" =~ ^[A-Za-z0-9._/-]+$ || "${MATRIX_CONFIG_PATH}" == *..* ]]; then
  echo "MATRIX_CONFIG_PATH must be a safe relative path" >&2
  exit 2
fi
if [[ ! "${AWS_SOURCE_S3_URI}" =~ ^s3://[A-Za-z0-9][A-Za-z0-9._/-]*$ \
   || ! "${AWS_RESULT_S3_PREFIX}" =~ ^s3://[A-Za-z0-9][A-Za-z0-9._/-]*$ ]]; then
  echo "source and result locations must be S3 URIs" >&2
  exit 2
fi
if [[ ! "${AWS_SOURCE_SHA256}" =~ ^[a-f0-9]{64}$ ]]; then
  echo "AWS_SOURCE_SHA256 must be 64 lowercase hex characters" >&2
  exit 2
fi
for integer in MAX_WALL_SECONDS MIN_VCPUS MIN_PHYSICAL_CORES ROOT_VOLUME_GIB; do
  if [[ ! "${!integer}" =~ ^[1-9][0-9]*$ ]]; then
    echo "${integer} must be a positive integer" >&2
    exit 2
  fi
done
if [[ "${USE_CAPACITY_RESERVATION}" != "0" && "${USE_CAPACITY_RESERVATION}" != "1" ]]; then
  echo "USE_CAPACITY_RESERVATION must be 0 or 1" >&2
  exit 2
fi
for amount in ASSERTED_HOURLY_PRICE_USD MAX_INSTANCE_COST_USD; do
  if [[ ! "${!amount}" =~ ^[0-9]+([.][0-9]+)?$ ]]; then
    echo "${amount} must be a non-negative decimal" >&2
    exit 2
  fi
done

PROJECTED_COST="$(awk -v hourly="${ASSERTED_HOURLY_PRICE_USD}" \
  -v seconds="${MAX_WALL_SECONDS}" 'BEGIN { printf "%.6f", hourly * seconds / 3600.0 }')"
if ! awk -v projected="${PROJECTED_COST}" -v cap="${MAX_INSTANCE_COST_USD}" \
  'BEGIN { exit !(projected <= cap) }'; then
  echo "projected instance cost ${PROJECTED_COST} exceeds cap ${MAX_INSTANCE_COST_USD}" >&2
  exit 2
fi

read -r DEFAULT_VCPUS DEFAULT_CORES THREADS_PER_CORE ARCHITECTURE <<< "$(
  aws ec2 describe-instance-types \
    --region "${AWS_REGION}" \
    --instance-types "${AWS_INSTANCE_TYPE}" \
    --query 'InstanceTypes[0].[VCpuInfo.DefaultVCpus,VCpuInfo.DefaultCores,VCpuInfo.DefaultThreadsPerCore,ProcessorInfo.SupportedArchitectures[0]]' \
    --output text
)"
if [[ "${ARCHITECTURE}" != "x86_64" ]]; then
  echo "instance type ${AWS_INSTANCE_TYPE} is ${ARCHITECTURE}, not x86_64" >&2
  exit 2
fi
if (( DEFAULT_VCPUS < MIN_VCPUS || DEFAULT_CORES < MIN_PHYSICAL_CORES )); then
  echo "instance exposes ${DEFAULT_VCPUS} vCPUs/${DEFAULT_CORES} cores, below ${MIN_VCPUS}/${MIN_PHYSICAL_CORES}" >&2
  exit 2
fi

VCPU_QUOTA="$(aws service-quotas list-service-quotas \
  --region "${AWS_REGION}" --service-code ec2 \
  --query "Quotas[?QuotaName=='${AWS_VCPU_QUOTA_NAME}'].Value | [0]" \
  --output json)"
if [[ ! "${VCPU_QUOTA}" =~ ^[0-9]+([.][0-9]+)?$ ]]; then
  echo "cannot resolve EC2 quota: ${AWS_VCPU_QUOTA_NAME}" >&2
  exit 2
fi
if ! awk -v quota="${VCPU_QUOTA}" -v required="${DEFAULT_VCPUS}" \
  'BEGIN { exit !(quota >= required) }'; then
  echo "EC2 quota '${AWS_VCPU_QUOTA_NAME}' is ${VCPU_QUOTA} vCPUs, below the ${DEFAULT_VCPUS} required" >&2
  exit 2
fi

OFFERING_COUNT="$(aws ec2 describe-instance-type-offerings \
  --region "${AWS_REGION}" \
  --location-type availability-zone \
  --filters "Name=location,Values=${AWS_AVAILABILITY_ZONE}" \
            "Name=instance-type,Values=${AWS_INSTANCE_TYPE}" \
  --query 'length(InstanceTypeOfferings)' --output text)"
if [[ "${OFFERING_COUNT}" != "1" ]]; then
  echo "${AWS_INSTANCE_TYPE} is not offered in ${AWS_AVAILABILITY_ZONE}" >&2
  exit 2
fi

read -r SUBNET_AZ SUBNET_VPC <<< "$(aws ec2 describe-subnets \
  --region "${AWS_REGION}" --subnet-ids "${AWS_SUBNET_ID}" \
  --query 'Subnets[0].[AvailabilityZone,VpcId]' --output text)"
if [[ "${SUBNET_AZ}" != "${AWS_AVAILABILITY_ZONE}" ]]; then
  echo "subnet ${AWS_SUBNET_ID} is in ${SUBNET_AZ}, not ${AWS_AVAILABILITY_ZONE}" >&2
  exit 2
fi

read -r SECURITY_GROUP_VPC INGRESS_COUNT EGRESS_COUNT <<< "$(aws ec2 describe-security-groups \
  --region "${AWS_REGION}" --group-ids "${AWS_SECURITY_GROUP_ID}" \
  --query 'SecurityGroups[0].[VpcId,length(IpPermissions),length(IpPermissionsEgress)]' --output text)"
if [[ "${SECURITY_GROUP_VPC}" != "${SUBNET_VPC}" ]]; then
  echo "security group ${AWS_SECURITY_GROUP_ID} is not in subnet VPC ${SUBNET_VPC}" >&2
  exit 2
fi
if [[ "${INGRESS_COUNT}" != "0" ]]; then
  echo "security group ${AWS_SECURITY_GROUP_ID} has inbound rules; use an egress-only SSM group" >&2
  exit 2
fi
if [[ "${EGRESS_COUNT}" == "0" ]]; then
  echo "security group ${AWS_SECURITY_GROUP_ID} has no egress rule" >&2
  exit 2
fi

PROFILE_COUNT="$(aws iam list-instance-profiles-for-role \
  --role-name "$(aws iam get-instance-profile \
    --instance-profile-name "${AWS_IAM_INSTANCE_PROFILE}" \
    --query 'InstanceProfile.Roles[0].RoleName' --output text)" \
  --query "length(InstanceProfiles[?InstanceProfileName=='${AWS_IAM_INSTANCE_PROFILE}'])" \
  --output text)"
if [[ "${PROFILE_COUNT}" != "1" ]]; then
  echo "instance profile ${AWS_IAM_INSTANCE_PROFILE} is not ready" >&2
  exit 2
fi

SOURCE_LOCATION="${AWS_SOURCE_S3_URI#s3://}"
SOURCE_BUCKET="${SOURCE_LOCATION%%/*}"
SOURCE_KEY="${SOURCE_LOCATION#*/}"
aws s3api head-object --bucket "${SOURCE_BUCKET}" --key "${SOURCE_KEY}" >/dev/null

AMI_ID="$(aws ssm get-parameter \
  --region "${AWS_REGION}" \
  --name /aws/service/ami-amazon-linux-latest/al2023-ami-kernel-default-x86_64 \
  --query 'Parameter.Value' --output text)"

echo "instance_type=${AWS_INSTANCE_TYPE}"
echo "architecture=${ARCHITECTURE} vcpus=${DEFAULT_VCPUS} physical_cores=${DEFAULT_CORES} threads_per_core=${THREADS_PER_CORE}"
echo "vcpu_quota_name=${AWS_VCPU_QUOTA_NAME} vcpu_quota=${VCPU_QUOTA}"
echo "ami=${AMI_ID}"
echo "wall_seconds=${MAX_WALL_SECONDS} asserted_hourly_usd=${ASSERTED_HOURLY_PRICE_USD} projected_instance_usd=${PROJECTED_COST} cap_usd=${MAX_INSTANCE_COST_USD}"
echo "capacity_reservation=${USE_CAPACITY_RESERVATION}"
if (( PLAN == 1 )); then
  echo "plan only: no AWS resources created"
  exit 0
fi

END_DATE="$(python3 -c 'import datetime,sys; print((datetime.datetime.now(datetime.timezone.utc)+datetime.timedelta(seconds=int(sys.argv[1])+900)).isoformat())' "${MAX_WALL_SECONDS}")"
CAPACITY_RESERVATION_ID=""
INSTANCE_ID=""
USER_DATA=""
LAUNCH_ATTEMPTED=0
cleanup_launch() {
  local status=$?
  trap - EXIT INT TERM
  set +e
  [[ -n "${USER_DATA}" ]] && rm -f "${USER_DATA}"
  drop7_cleanup_run_resources "${AWS_REGION}" "${RUN_ID}" \
    "${INSTANCE_ID}" "${CAPACITY_RESERVATION_ID}" \
    "${LAUNCH_ATTEMPTED}" "${USE_CAPACITY_RESERVATION}" || true
  exit "${status}"
}
trap cleanup_launch EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

if [[ "${USE_CAPACITY_RESERVATION}" == "1" ]]; then
  CAPACITY_RESERVATION_ID="$(aws ec2 create-capacity-reservation \
    --region "${AWS_REGION}" \
    --availability-zone "${AWS_AVAILABILITY_ZONE}" \
    --instance-type "${AWS_INSTANCE_TYPE}" \
    --instance-platform Linux/UNIX \
    --instance-count 1 \
    --instance-match-criteria targeted \
    --end-date-type limited \
    --end-date "${END_DATE}" \
    --tag-specifications "ResourceType=capacity-reservation,Tags=[{Key=Project,Value=drop7-rust-search},{Key=RunId,Value=${RUN_ID}}]" \
    --query 'CapacityReservation.CapacityReservationId' --output text)"
fi

USER_DATA="$(mktemp "${CONFIG_DIR}/user-data.XXXXXX.sh")"
{
  echo '#!/usr/bin/env bash'
  echo 'set -euo pipefail'
  echo 'preflight_shutdown() {'
  echo '  status=$?'
  echo '  trap - EXIT INT TERM'
  echo '  shutdown -h now || true'
  echo '  exit "${status}"'
  echo '}'
  echo 'trap preflight_shutdown EXIT INT TERM'
  printf '( sleep %q; shutdown -h now ) >/var/log/drop7-wall-watchdog.log 2>&1 &\n' "${MAX_WALL_SECONDS}"
  echo 'dnf install -y cargo git python3 tar gzip'
  echo 'if ! command -v aws >/dev/null 2>&1; then'
  echo '  dnf install -y awscli2 || dnf install -y awscli'
  echo 'fi'
  echo 'mkdir -p /opt/drop7-source /opt/drop7'
  printf 'aws s3 cp %q /opt/drop7-source/drop7-source.tar.gz --only-show-errors\n' "${AWS_SOURCE_S3_URI}"
  printf 'echo %q | sha256sum -c -\n' "${AWS_SOURCE_SHA256}  /opt/drop7-source/drop7-source.tar.gz"
  echo 'tar -C /opt/drop7 -xzf /opt/drop7-source/drop7-source.tar.gz'
  echo 'cd /opt/drop7'
  printf 'exec approaches/fair-expectimax/rust-engine/cluster/run-instance.sh %q %q %q %q\n' \
    "${MATRIX_CONFIG_PATH}" "${AWS_RESULT_S3_PREFIX}" "${MAX_WALL_SECONDS}" "${RUN_ID}"
} > "${USER_DATA}"

RUN_ARGS=(
  --region "${AWS_REGION}"
  --image-id "${AMI_ID}"
  --instance-type "${AWS_INSTANCE_TYPE}"
  --subnet-id "${AWS_SUBNET_ID}"
  --associate-public-ip-address
  --security-group-ids "${AWS_SECURITY_GROUP_ID}"
  --iam-instance-profile "Name=${AWS_IAM_INSTANCE_PROFILE}"
  --instance-initiated-shutdown-behavior terminate
  --metadata-options 'HttpTokens=required,HttpEndpoint=enabled'
  --block-device-mappings "DeviceName=/dev/xvda,Ebs={DeleteOnTermination=true,Encrypted=true,VolumeSize=${ROOT_VOLUME_GIB},VolumeType=gp3}"
  --user-data "file://${USER_DATA}"
  --client-token "${RUN_ID}"
  --tag-specifications "ResourceType=instance,Tags=[{Key=Project,Value=drop7-rust-search},{Key=RunId,Value=${RUN_ID}}]" "ResourceType=volume,Tags=[{Key=Project,Value=drop7-rust-search},{Key=RunId,Value=${RUN_ID}}]"
)
if [[ -n "${CAPACITY_RESERVATION_ID}" ]]; then
  RUN_ARGS+=(--capacity-reservation-specification "CapacityReservationTarget={CapacityReservationId=${CAPACITY_RESERVATION_ID}}")
fi
LAUNCH_ATTEMPTED=1
INSTANCE_ID="$(aws ec2 run-instances "${RUN_ARGS[@]}" \
  --query 'Instances[0].InstanceId' --output text)"
if [[ ! "${INSTANCE_ID}" =~ ^i-[a-f0-9]+$ ]]; then
  echo "run-instances returned an invalid instance id: ${INSTANCE_ID}" >&2
  exit 2
fi
trap - EXIT INT TERM
rm -f "${USER_DATA}"

echo "instance_id=${INSTANCE_ID}"
echo "capacity_reservation_id=${CAPACITY_RESERVATION_ID:-none}"
echo "results=${AWS_RESULT_S3_PREFIX}/${RUN_ID}/"
echo "The instance terminates itself after the matrix or wall timeout; a limited reservation expires at ${END_DATE}."
