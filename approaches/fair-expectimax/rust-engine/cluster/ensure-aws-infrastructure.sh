#!/usr/bin/env bash
# Resolve or create the reusable, no-ingress AWS plumbing for one EC2 runner.
set -euo pipefail

usage() {
  echo "usage: $0 --region REGION --instance-type TYPE --bucket BUCKET --state-dir RUNS_DIR" >&2
}

REGION=""
INSTANCE_TYPE=""
BUCKET=""
STATE_DIR=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    --region)
      REGION="${2:-}"
      shift 2
      ;;
    --instance-type)
      INSTANCE_TYPE="${2:-}"
      shift 2
      ;;
    --bucket)
      BUCKET="${2:-}"
      shift 2
      ;;
    --state-dir)
      STATE_DIR="${2:-}"
      shift 2
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

if [[ -z "${REGION}" || -z "${INSTANCE_TYPE}" || -z "${BUCKET}" \
   || -z "${STATE_DIR}" ]]; then
  usage
  exit 2
fi

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
if [[ "${STATE_DIR}" == *..* ]]; then
  echo "state directory may not contain '..'" >&2
  exit 2
fi
if [[ "${STATE_DIR}" == "${ROOT}/runs/"* ]]; then
  :
elif [[ "${STATE_DIR}" == runs/* ]]; then
  STATE_DIR="${ROOT}/${STATE_DIR}"
else
  echo "state directory must be inside ${ROOT}/runs" >&2
  exit 2
fi
mkdir -p "${STATE_DIR}"
STATE_DIR="$(cd "${STATE_DIR}" && pwd)"
if [[ "${STATE_DIR}" != "${ROOT}/runs/"* ]]; then
  echo "state directory must be inside ${ROOT}/runs" >&2
  exit 2
fi
if [[ ! "${REGION}" =~ ^[a-z0-9-]+$ \
   || ! "${INSTANCE_TYPE}" =~ ^[a-z0-9.]+$ \
   || ! "${BUCKET}" =~ ^[a-z0-9][a-z0-9.-]+$ ]]; then
  echo "region, instance type, or bucket contains unsafe characters" >&2
  exit 2
fi
if ! command -v aws >/dev/null 2>&1; then
  echo "AWS CLI is required" >&2
  exit 2
fi

aws s3api head-bucket --bucket "${BUCKET}" >/dev/null
BUCKET_REGION="$(aws s3api get-bucket-location \
  --bucket "${BUCKET}" --query 'LocationConstraint' --output text)"
if [[ "${BUCKET_REGION}" == "None" ]]; then
  BUCKET_REGION="us-east-1"
fi
if [[ "${BUCKET_REGION}" != "${REGION}" ]]; then
  echo "bucket ${BUCKET} is in ${BUCKET_REGION}, not ${REGION}" >&2
  exit 2
fi

OFFERED_AZS="$(aws ec2 describe-instance-type-offerings \
  --region "${REGION}" \
  --location-type availability-zone \
  --filters "Name=instance-type,Values=${INSTANCE_TYPE}" \
  --query 'InstanceTypeOfferings[].Location' --output text)"
if [[ -z "${OFFERED_AZS}" || "${OFFERED_AZS}" == "None" ]]; then
  echo "${INSTANCE_TYPE} has no Availability Zone offering in ${REGION}" >&2
  exit 2
fi

SUBNET_ID="${DROP7_SUBNET_ID:-}"
AVAILABILITY_ZONE="${DROP7_AVAILABILITY_ZONE:-}"
VPC_ID=""
if [[ -n "${SUBNET_ID}" ]]; then
  read -r SUBNET_AZ VPC_ID SUBNET_STATE <<< "$(aws ec2 describe-subnets \
    --region "${REGION}" --subnet-ids "${SUBNET_ID}" \
    --query 'Subnets[0].[AvailabilityZone,VpcId,State]' --output text)"
  if [[ "${SUBNET_STATE}" != "available" ]]; then
    echo "subnet ${SUBNET_ID} is not available" >&2
    exit 2
  fi
  if [[ -n "${AVAILABILITY_ZONE}" && "${AVAILABILITY_ZONE}" != "${SUBNET_AZ}" ]]; then
    echo "DROP7_AVAILABILITY_ZONE does not match DROP7_SUBNET_ID" >&2
    exit 2
  fi
  AVAILABILITY_ZONE="${SUBNET_AZ}"
else
  VPC_ID="$(aws ec2 describe-vpcs --region "${REGION}" \
    --filters Name=is-default,Values=true \
    --query 'Vpcs[0].VpcId' --output text)"
  if [[ -z "${VPC_ID}" || "${VPC_ID}" == "None" ]]; then
    echo "no default VPC in ${REGION}; set DROP7_SUBNET_ID explicitly" >&2
    exit 2
  fi
  for offered_az in ${OFFERED_AZS}; do
    if [[ -n "${AVAILABILITY_ZONE}" && "${offered_az}" != "${AVAILABILITY_ZONE}" ]]; then
      continue
    fi
    candidate="$(aws ec2 describe-subnets --region "${REGION}" \
      --filters "Name=vpc-id,Values=${VPC_ID}" \
                "Name=availability-zone,Values=${offered_az}" \
                "Name=state,Values=available" \
                "Name=map-public-ip-on-launch,Values=true" \
      --query 'Subnets[0].SubnetId' --output text)"
    if [[ -n "${candidate}" && "${candidate}" != "None" ]]; then
      SUBNET_ID="${candidate}"
      AVAILABILITY_ZONE="${offered_az}"
      break
    fi
  done
  if [[ -z "${SUBNET_ID}" ]]; then
    echo "no public default-VPC subnet intersects the ${INSTANCE_TYPE} offerings; set DROP7_SUBNET_ID" >&2
    exit 2
  fi
fi

if ! grep -qw "${AVAILABILITY_ZONE}" <<< "${OFFERED_AZS}"; then
  echo "${INSTANCE_TYPE} is not offered in subnet AZ ${AVAILABILITY_ZONE}" >&2
  exit 2
fi

SECURITY_GROUP_ID="${DROP7_SECURITY_GROUP_ID:-}"
if [[ -z "${SECURITY_GROUP_ID}" ]]; then
  SECURITY_GROUP_ID="$(aws ec2 describe-security-groups --region "${REGION}" \
    --filters "Name=vpc-id,Values=${VPC_ID}" \
              'Name=tag:Project,Values=drop7-rust-search' \
              'Name=tag:Purpose,Values=ec2-runner-egress-only' \
    --query 'SecurityGroups[0].GroupId' --output text)"
  if [[ -z "${SECURITY_GROUP_ID}" || "${SECURITY_GROUP_ID}" == "None" ]]; then
    # Group names are unique within a VPC. Reuse an untagged group with the
    # reserved name and validate it below instead of failing on a duplicate.
    SECURITY_GROUP_ID="$(aws ec2 describe-security-groups --region "${REGION}" \
      --filters "Name=vpc-id,Values=${VPC_ID}" \
                'Name=group-name,Values=drop7-bench-ec2-egress' \
      --query 'SecurityGroups[0].GroupId' --output text)"
    if [[ -z "${SECURITY_GROUP_ID}" || "${SECURITY_GROUP_ID}" == "None" ]]; then
      SECURITY_GROUP_ID="$(aws ec2 create-security-group --region "${REGION}" \
        --vpc-id "${VPC_ID}" \
        --group-name drop7-bench-ec2-egress \
        --description 'Egress-only group for ephemeral Drop7 EC2 search runners' \
        --tag-specifications \
          'ResourceType=security-group,Tags=[{Key=Project,Value=drop7-rust-search},{Key=Purpose,Value=ec2-runner-egress-only}]' \
        --query GroupId --output text)"
    fi
  fi
fi

read -r SECURITY_GROUP_VPC INGRESS_COUNT EGRESS_COUNT <<< "$(aws ec2 describe-security-groups \
  --region "${REGION}" --group-ids "${SECURITY_GROUP_ID}" \
  --query 'SecurityGroups[0].[VpcId,length(IpPermissions),length(IpPermissionsEgress)]' --output text)"
if [[ "${SECURITY_GROUP_VPC}" != "${VPC_ID}" ]]; then
  echo "security group ${SECURITY_GROUP_ID} is not in subnet VPC ${VPC_ID}" >&2
  exit 2
fi
if [[ "${INGRESS_COUNT}" != "0" ]]; then
  echo "security group ${SECURITY_GROUP_ID} has inbound rules" >&2
  exit 2
fi
if [[ "${EGRESS_COUNT}" == "0" ]]; then
  echo "security group ${SECURITY_GROUP_ID} has no egress rule" >&2
  exit 2
fi

ROLE_NAME="${DROP7_IAM_ROLE_NAME:-drop7-bench-ec2-runner}"
PROFILE_NAME="${DROP7_IAM_INSTANCE_PROFILE:-drop7-bench-ec2-runner}"
TRUST_FILE="$(mktemp "${STATE_DIR}/iam-trust.XXXXXX.json")"
POLICY_FILE="$(mktemp "${STATE_DIR}/iam-policy.XXXXXX.json")"
trap 'rm -f "${TRUST_FILE}" "${POLICY_FILE}"' EXIT

python3 -c 'import json,sys; json.dump({"Version":"2012-10-17","Statement":[{"Effect":"Allow","Principal":{"Service":"ec2.amazonaws.com"},"Action":"sts:AssumeRole"}]},open(sys.argv[1],"w"))' "${TRUST_FILE}"
python3 -c 'import json,sys; bucket=sys.argv[2]; json.dump({"Version":"2012-10-17","Statement":[{"Effect":"Allow","Action":["s3:GetBucketLocation","s3:ListBucket","s3:ListBucketMultipartUploads"],"Resource":f"arn:aws:s3:::{bucket}"},{"Effect":"Allow","Action":["s3:GetObject","s3:PutObject","s3:AbortMultipartUpload"],"Resource":f"arn:aws:s3:::{bucket}/ec2/*"}]},open(sys.argv[1],"w"))' "${POLICY_FILE}" "${BUCKET}"

if ! aws iam get-role --role-name "${ROLE_NAME}" >/dev/null 2>&1; then
  aws iam create-role --role-name "${ROLE_NAME}" \
    --assume-role-policy-document "file://${TRUST_FILE}" \
    --description 'Ephemeral Drop7 EC2 search runner' >/dev/null
fi
aws iam update-assume-role-policy --role-name "${ROLE_NAME}" \
  --policy-document "file://${TRUST_FILE}"
aws iam put-role-policy --role-name "${ROLE_NAME}" \
  --policy-name drop7-bench-s3-access \
  --policy-document "file://${POLICY_FILE}"
aws iam attach-role-policy --role-name "${ROLE_NAME}" \
  --policy-arn arn:aws:iam::aws:policy/AmazonSSMManagedInstanceCore

if ! aws iam get-instance-profile --instance-profile-name "${PROFILE_NAME}" >/dev/null 2>&1; then
  aws iam create-instance-profile --instance-profile-name "${PROFILE_NAME}" >/dev/null
fi
PROFILE_ROLE_COUNT="$(aws iam get-instance-profile \
  --instance-profile-name "${PROFILE_NAME}" \
  --query "length(InstanceProfile.Roles[?RoleName=='${ROLE_NAME}'])" --output text)"
if [[ "${PROFILE_ROLE_COUNT}" == "0" ]]; then
  aws iam add-role-to-instance-profile \
    --instance-profile-name "${PROFILE_NAME}" --role-name "${ROLE_NAME}"
  # IAM instance-profile propagation is eventually consistent.
  sleep 10
fi

printf '%s\t%s\t%s\t%s\n' \
  "${AVAILABILITY_ZONE}" "${SUBNET_ID}" "${SECURITY_GROUP_ID}" "${PROFILE_NAME}"
