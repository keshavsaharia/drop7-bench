#!/usr/bin/env bash
# Runs the full three-engine benchmark suite and assembles benchmark.json.
#
# Arms:
#   moves    whole center-policy games (engine throughput + scaling)
#   leaf     tight-loop leaf evaluation on the shared roots
#   search   fixed-work decisions at d4s7 and d5s7 on the shared roots
#
# Engines: TypeScript (src/core/typescript), C++ reference + C++ fast
# (approaches/lifetime-objective/fast-engine), Rust (this crate, no-table and
# depth-gated arms).  All search arms read the SAME harvested roots file.
#
# Timing discipline: best of --repeats, load average recorded.  Seeds are the
# Rust benchmark sub-block 0xa5277000-0xa5277fff of the already-opened
# SEEDLEASE-A52-FAST development lease; the harvested roots come from the
# Rust gate sub-block 0xa5276000-0xa5276fff.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
HERE="${ROOT}/approaches/fair-expectimax/rust-engine"
RUST_BIN="${HERE}/target/release"
CPP_BIN="${ROOT}/build/rust-engine"
RUN_DIR="${1:?usage: bench_all.sh <run-dir> <roots-file> [repeats]}"
ROOTS="${2:?usage: bench_all.sh <run-dir> <roots-file> [repeats]}"
REPEATS="${3:-3}"

export RUSTUP_HOME="${RUSTUP_HOME:-$HOME/.rustup}"
export CARGO_HOME="${CARGO_HOME:-$HOME/.cargo}"
export PATH="${CARGO_HOME}/bin:${PATH}"

OUT="${RUN_DIR}/benchmark.json"
GAMES=32768
D4_DECISIONS=21
D5_DECISIONS=3

echo "== engine move throughput (1 thread, ${GAMES} games) ==" | tee -a "${RUN_DIR}/bench.log"
node --experimental-strip-types "${HERE}/ts/trace.ts" bench --games "${GAMES}" \
  | tee -a "${RUN_DIR}/bench.log"
"${CPP_BIN}/bench-moves" --games "${GAMES}" --repeats "${REPEATS}" --threads 1 \
  | tee -a "${RUN_DIR}/bench.log"
"${RUST_BIN}/bench" moves --games "${GAMES}" --threads 1 \
  | tee -a "${RUN_DIR}/bench.log"

echo "== leaf throughput (shared roots) ==" | tee -a "${RUN_DIR}/bench.log"
"${CPP_BIN}/leaf-micro" --roots "${ROOTS}" --repeats 50 | tee -a "${RUN_DIR}/bench.log"
"${RUST_BIN}/bench" micro --roots "${ROOTS}" --repeats 50 | tee -a "${RUN_DIR}/bench.log"

echo "== search d4s7 (${D4_DECISIONS} decisions) ==" | tee -a "${RUN_DIR}/bench.log"
"${CPP_BIN}/search-bench" --roots "${ROOTS}" --depth 4 --strata 7 \
  --decisions "${D4_DECISIONS}" --repeats "${REPEATS}" | tee -a "${RUN_DIR}/bench.log"
"${RUST_BIN}/bench" search --roots "${ROOTS}" --depth 4 --strata 7 \
  --decisions "${D4_DECISIONS}" --repeats "${REPEATS}" --tt none \
  | tee -a "${RUN_DIR}/bench.log"
"${RUST_BIN}/bench" search --roots "${ROOTS}" --depth 4 --strata 7 \
  --decisions "${D4_DECISIONS}" --repeats "${REPEATS}" --tt gate1 --tt-capacity 65536 \
  | tee -a "${RUN_DIR}/bench.log"

echo "== search d5s7 (${D5_DECISIONS} decisions) ==" | tee -a "${RUN_DIR}/bench.log"
"${CPP_BIN}/search-bench" --roots "${ROOTS}" --depth 5 --strata 7 \
  --decisions "${D5_DECISIONS}" --repeats 1 | tee -a "${RUN_DIR}/bench.log"
"${RUST_BIN}/bench" search --roots "${ROOTS}" --depth 5 --strata 7 \
  --decisions "${D5_DECISIONS}" --repeats 1 --tt none | tee -a "${RUN_DIR}/bench.log"
"${RUST_BIN}/bench" search --roots "${ROOTS}" --depth 5 --strata 7 \
  --decisions "${D5_DECISIONS}" --repeats 1 --tt gate1 --tt-capacity 262144 \
  | tee -a "${RUN_DIR}/bench.log"

echo "== game-level scaling (fast engine vs rust, ${GAMES} games) ==" | tee -a "${RUN_DIR}/bench.log"
for threads in 1 2 4 8 16 32; do
  "${CPP_BIN}/bench-moves" --games "${GAMES}" --repeats 1 --threads "${threads}" \
    | grep "fast engine" | sed "s/^/cpp /" | tee -a "${RUN_DIR}/bench.log"
  "${RUST_BIN}/bench" moves --games "${GAMES}" --threads "${threads}" \
    | grep "moves mode" | sed "s/^/rust /" | tee -a "${RUN_DIR}/bench.log"
done

echo "benchmark raw log at ${RUN_DIR}/bench.log"
