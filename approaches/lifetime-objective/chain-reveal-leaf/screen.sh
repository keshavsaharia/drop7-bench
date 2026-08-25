#!/usr/bin/env bash
# The successor reveal-construction SCREEN: four arms, 256 paired games, depth
# 4 / seven strata, memo engine, shadow unchanged search on every non-frozen
# decision.  Opens the leased range 0xa52d0200+ exactly once.  Do not run this
# outside the frozen successor protocol; the coordinator launches it.
#
#   screen.sh <output-stem> <seed-lease-id>
#
# Writes <stem>.json (arms index) and <stem>.{frozen,A300,A900,B}.json
# (drop7-lifetime-cohort-v1), rewritten after every 4 completed games so an
# interrupted run leaves a partial, "complete": false artifact.  The runner
# INTERLEAVES arms: task k = (game k / 4, arm k % 4), so at 30 threads about
# 7-8 games x 4 arms are in flight at once and a manual stop "at 64 games" means
# 64-72 completed games per arm (kimi-k3-prereg-review.md section 4).  Stop
# conditions are operator-applied with compare.py --first N on the partial
# artifacts; the runner implements no stop logic.
set -euo pipefail
STEM="${1:?usage: screen.sh <output-stem> <seed-lease-id>}"
LEASE="${2:?usage: screen.sh <output-stem> <seed-lease-id>}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
RUN="${ROOT}/build/chain-reveal-leaf/run"
[ -x "${RUN}" ] || { echo "build first: approaches/lifetime-objective/chain-reveal-leaf/build.sh" >&2; exit 1; }
exec "${RUN}" \
  --arm frozen \
  --arm A300:aligned_double_hit=300 \
  --arm A900:aligned_double_hit=900 \
  --arm B:aligned_double_hit=300,chain_to_crack_cracked_gated=150,chain_to_crack_solid_gated=300 \
  --seed-start 0xa52d0200 --games 256 --threads 30 \
  --depth 4 --chance-samples 7 --cache 60000 --max-moves 2000 \
  --shadow --seed-lease "${LEASE}" --data-role public-development \
  --label reveal-construction-screen --chunk 4 \
  --output "${STEM}.json"
