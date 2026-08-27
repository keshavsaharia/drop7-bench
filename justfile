set positional-arguments

# Example: AWS_PROFILE=personal-deploy just run-matrix-ec2 gauntlet-01 --budget 10000
# Package, upload, launch, monitor, collect, and clean up one EC2 search matrix.
run-matrix-ec2 round *args:
    #!/usr/bin/env bash
    exec approaches/fair-expectimax/rust-engine/cluster/run-ec2-matrix.sh "$@"

# Example: just run-matrix-local gauntlet-01 --depths 2,3,4,5 --strata 7
# Run the depth matrix on this machine (`--help` lists every engine option); no AWS resource is touched.
run-matrix-local *args:
    #!/usr/bin/env bash
    exec approaches/fair-expectimax/rust-engine/cluster/run-local-matrix.sh "$@"

# Interactive dispatcher: offers EC2 with a valid AWS credential, else confirms a local run.
run-matrix *args:
    #!/usr/bin/env bash
    set -euo pipefail
    if command -v aws >/dev/null 2>&1 && timeout 15 aws sts get-caller-identity >/dev/null 2>&1; then
      printf 'Valid AWS credentials found. Run the matrix on [e]c2 or [l]ocal hardware? ' >&2
      read -r choice
      case "${choice}" in
        e|E|ec2|EC2) exec approaches/fair-expectimax/rust-engine/cluster/run-ec2-matrix.sh "$@" ;;
        l|L|local|LOCAL) exec approaches/fair-expectimax/rust-engine/cluster/run-local-matrix.sh "$@" ;;
        *) echo "aborted: answer e or l" >&2; exit 2 ;;
      esac
    else
      printf 'No valid AWS credential found. Run the matrix solver locally? [y/N] ' >&2
      read -r confirm
      case "${confirm}" in
        y|Y|yes|YES) exec approaches/fair-expectimax/rust-engine/cluster/run-local-matrix.sh "$@" ;;
        *) echo "aborted" >&2; exit 2 ;;
      esac
    fi
