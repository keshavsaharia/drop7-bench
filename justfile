set positional-arguments

# Package, upload, launch, monitor, collect, and clean up one EC2 search matrix.
# Example: AWS_PROFILE=personal-deploy just run-matrix gauntlet-01 --budget 10000
run-matrix round *args:
    #!/usr/bin/env bash
    exec approaches/fair-expectimax/rust-engine/cluster/run-ec2-matrix.sh "$@"
