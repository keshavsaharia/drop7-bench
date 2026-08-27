#!/usr/bin/env bash
# Shared recovery cleanup for one tagged Drop7 EC2 run.

drop7_find_run_instance_ids() {
  local region="$1"
  local run_id="$2"
  local raw id

  raw="$(aws ec2 describe-instances \
    --region "${region}" \
    --filters \
      "Name=client-token,Values=${run_id}" \
      'Name=tag:Project,Values=drop7-rust-search' \
      "Name=tag:RunId,Values=${run_id}" \
      'Name=instance-state-name,Values=pending,running,shutting-down,stopping,stopped' \
    --query 'Reservations[].Instances[].InstanceId' --output text 2>/dev/null || true)"
  for id in ${raw}; do
    if [[ "${id}" =~ ^i-[a-f0-9]+$ ]]; then
      printf '%s\n' "${id}"
    fi
  done
}

drop7_find_run_capacity_reservation_ids() {
  local region="$1"
  local run_id="$2"
  local raw id

  raw="$(aws ec2 describe-capacity-reservations \
    --region "${region}" \
    --filters \
      'Name=tag:Project,Values=drop7-rust-search' \
      "Name=tag:RunId,Values=${run_id}" \
      'Name=state,Values=active' \
    --query 'CapacityReservations[].CapacityReservationId' \
    --output text 2>/dev/null || true)"
  for id in ${raw}; do
    if [[ "${id}" =~ ^cr-[a-f0-9]+$ ]]; then
      printf '%s\n' "${id}"
    fi
  done
}

# Terminate/cancel explicit IDs when available, otherwise recover them through
# the client token and atomic launch tags. Discovery retries because EC2
# describe operations are eventually consistent immediately after creation.
drop7_cleanup_run_resources() {
  local region="$1"
  local run_id="$2"
  local known_instance_id="${3:-}"
  local known_reservation_id="${4:-}"
  local expect_instance="${5:-1}"
  local expect_reservation="${6:-0}"
  local max_attempts="${7:-8}"
  local instance_done=0
  local reservation_done=0
  local attempt delay id
  local -a instance_ids reservation_ids

  if [[ "${known_instance_id}" =~ ^i-[a-f0-9]+$ ]]; then
    if aws ec2 terminate-instances --region "${region}" \
      --instance-ids "${known_instance_id}" >/dev/null 2>&1; then
      instance_done=1
    fi
  elif [[ "${expect_instance}" == "0" ]]; then
    instance_done=1
  fi

  if [[ "${known_reservation_id}" =~ ^cr-[a-f0-9]+$ ]]; then
    if aws ec2 cancel-capacity-reservation --region "${region}" \
      --capacity-reservation-id "${known_reservation_id}" >/dev/null 2>&1; then
      reservation_done=1
    fi
  elif [[ "${expect_reservation}" == "0" ]]; then
    reservation_done=1
  fi

  for (( attempt = 1; attempt <= max_attempts; attempt += 1 )); do
    if (( instance_done == 0 )); then
      instance_ids=()
      while IFS= read -r id; do
        [[ -n "${id}" ]] && instance_ids[${#instance_ids[@]}]="${id}"
      done < <(drop7_find_run_instance_ids "${region}" "${run_id}")
      if (( ${#instance_ids[@]} > 0 )); then
        if aws ec2 terminate-instances --region "${region}" \
          --instance-ids "${instance_ids[@]}" >/dev/null 2>&1; then
          instance_done=1
        fi
      fi
    fi

    if (( reservation_done == 0 )); then
      reservation_ids=()
      while IFS= read -r id; do
        [[ -n "${id}" ]] && reservation_ids[${#reservation_ids[@]}]="${id}"
      done < <(drop7_find_run_capacity_reservation_ids "${region}" "${run_id}")
      if (( ${#reservation_ids[@]} > 0 )); then
        reservation_done=1
        for id in "${reservation_ids[@]}"; do
          if ! aws ec2 cancel-capacity-reservation --region "${region}" \
            --capacity-reservation-id "${id}" >/dev/null 2>&1; then
            reservation_done=0
          fi
        done
      fi
    fi

    if (( instance_done == 1 && reservation_done == 1 )); then
      return 0
    fi
    if (( attempt < max_attempts )); then
      if [[ -n "${DROP7_CLEANUP_RETRY_DELAY_SECONDS:-}" ]]; then
        delay="${DROP7_CLEANUP_RETRY_DELAY_SECONDS}"
      elif (( attempt >= 6 )); then
        delay=30
      else
        delay="$((1 << (attempt - 1)))"
      fi
      sleep "${delay}"
    fi
  done

  if (( instance_done == 0 )); then
    echo "cleanup warning: no active instance became visible for run ${run_id}" >&2
  fi
  if (( reservation_done == 0 )); then
    echo "cleanup warning: no active capacity reservation became visible for run ${run_id}" >&2
  fi
  return 1
}
