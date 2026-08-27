#!/usr/bin/env bash
# Release build with the host CPU's instruction set (the drop7-rs board code
# uses BMI2 PEXT/PDEP when available; a bit-identical portable loop compiles
# without it).  Hermetic: no network fetch, the only dependency is the local
# drop7-rs engine crate.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

# Toolchain discovery: prefer a standard rustup install, fall back to the
# session toolchain the rust-engine was built with.
if [ -d "$HOME/.cargo/bin" ]; then
  export RUSTUP_HOME="${RUSTUP_HOME:-$HOME/.rustup}"
  export CARGO_HOME="${CARGO_HOME:-$HOME/.cargo}"
elif [ -d /tmp/opencode/cargo/bin ]; then
  export RUSTUP_HOME="${RUSTUP_HOME:-/tmp/opencode/rustup}"
  export CARGO_HOME="${CARGO_HOME:-/tmp/opencode/cargo}"
fi
export PATH="${CARGO_HOME}/bin:${PATH}"

RUSTFLAGS="-C target-cpu=native" cargo build --release "$@"
