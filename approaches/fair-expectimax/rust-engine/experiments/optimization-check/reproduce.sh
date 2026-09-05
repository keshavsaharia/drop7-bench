#!/usr/bin/env bash
# Rebuild the unchanged dev comparator and the current candidate, then rerun
# the constructed-root CHECK. Pass a fresh namespaced run directory.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(git -C "$HERE" rev-parse --show-toplevel)"
cd "$ROOT"
OUT="${1:?Usage: reproduce.sh runs/RUN-<new-id>}"
[[ "$OUT" == runs/RUN-* && ! -e "$OUT" ]] || { echo "Use a fresh runs/RUN-<id> path" >&2; exit 2; }
BASE=7adb6b36412b4bbef25623183dac1a905570cd1a
ENGINE=approaches/fair-expectimax/rust-engine
BUILD="build/$(basename "$OUT")"
mkdir -p "$OUT" "$BUILD/baseline-tree"
git archive "$BASE" "$ENGINE" | tar -x -C "$BUILD/baseline-tree"
BASE_CRATE="$BUILD/baseline-tree/$ENGINE"
cp "$ENGINE/src/bin/optimization_check.rs" "$BASE_CRATE/src/bin/"
cp -R "$BASE_CRATE" "$BUILD/leaf-only"
cp "$ENGINE/src/leaf.rs" "$ENGINE/src/board.rs" "$BUILD/leaf-only/src/"
export RUSTFLAGS='-C target-cpu=native'
export RUST_TEST_THREADS=1
cargo build --release --jobs 4 --manifest-path "$BASE_CRATE/Cargo.toml" --bin optimization_check
cargo build --release --jobs 4 --manifest-path "$BUILD/leaf-only/Cargo.toml" --bin optimization_check
CARGO_TARGET_DIR="$ROOT/$BUILD/candidate" cargo test --release --jobs 4 --manifest-path "$ENGINE/Cargo.toml" > "$OUT/cargo-tests.log" 2>&1
CARGO_TARGET_DIR="$ROOT/$BUILD/candidate" cargo build --release --jobs 4 --manifest-path "$ENGINE/Cargo.toml" --bins
BIN="$BUILD/candidate/release"
BASE_BIN="$BASE_CRATE/target/release/optimization_check"
# -Wno-error=format is only for the existing Linux /proc sscanf portability
# warnings in corpus.hpp. Contracting multiply/add changes reference leaf bits.
clang++ -O3 -ffp-contract=off -std=c++20 -pthread -Wall -Wextra -Werror -Wno-error=format -I . \
  "$HERE/reference.cpp" -o "$BUILD/reference"
"$BUILD/reference" "$HERE/roots.txt" leaf 4 > "$OUT/cpp-leaf.trace"
"$BUILD/reference" "$HERE/roots.txt" search 4 > "$OUT/cpp-search.trace"
"$BIN/gate_leaf" --trace "$OUT/cpp-leaf.trace" > "$OUT/leaf-gate.log"
"$BIN/gate_search" --trace "$OUT/cpp-search.trace" --mode values --depth 4 --strata 7 --tt gate1 --tt-capacity 16384 > "$OUT/search-gate.log"
python3 .agents/skills/million-point-research/scripts/researchctl.py doctor > "$OUT/machine-profile.json"
# Run these sequentially with other builds and timing jobs stopped. Counter
# collection requires macOS permission to read kern.clockrate.
python3 "$HERE/measure.py" --phase compare --baseline "$BASE_BIN" \
  --candidate "$BUILD/leaf-only/target/release/optimization_check" \
  --roots "$HERE/roots.txt" --output "$OUT/leaf-compare"
for phase in compare sweep parallel; do
  python3 "$HERE/measure.py" --phase "$phase" --baseline "$BASE_BIN" \
    --candidate "$BIN/optimization_check" --analyzer "$BIN/analyze" \
    --roots "$HERE/roots.txt" --output "$OUT/$phase"
done
python3 "$HERE/summarize.py" "$OUT"
