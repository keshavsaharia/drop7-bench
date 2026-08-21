#!/usr/bin/env bash
# Completes finding-14 once the depth-5 arms have all 64 games.
#
# The cohort runner rewrites its artifact after every 16-game chunk and logs
# every finished game, so this can be run at any time: it reports how many games
# each arm currently has and analyses whatever is there.  It is idempotent.
set -u
R="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
C="$R/approaches/lifetime-objective/fast-engine/cohorts"
A="$R/approaches/lifetime-objective/fast-engine/analyze.py"
REF="$R/runs/RUN-A51D-s7confirm"

echo "===== arm progress ====="
for arm in d4s7-control d5s7 d5s5; do
  n=$(grep -c 'seed 0x' "$C/$arm.log" 2>/dev/null || echo 0)
  echo "  $arm: $n/64 games finished"
done
echo
echo "===== completed-depth audit (an arm with any incomplete decision is void) ====="
grep -h 'AUDIT\|ARM ' "$C"/*.log 2>/dev/null
echo
echo "===== end-to-end reproduction of the unoptimised binary ====="
python3 "$A" identical "$C/d4s7-control.json" "$REF/fresh-s7.json"
[ -f "$C/d5s5.json" ] && python3 "$A" identical "$C/d5s5.json" "$REF/s5d5.json"
echo "===== summaries ====="
for f in "$C/d4s7-control.json" "$C/d5s7.json" "$C/d5s5.json"; do
  [ -f "$f" ] && python3 "$A" summary "$f"
done
echo "===== paired comparisons (one-sided 95% bootstrap over whole games) ====="
[ -f "$C/d5s7.json" ] && python3 "$A" paired "$C/d5s7.json" "$C/d4s7-control.json"
[ -f "$C/d5s7.json" ] && [ -f "$C/d5s5.json" ] && python3 "$A" paired "$C/d5s7.json" "$C/d5s5.json"
[ -f "$C/d5s5.json" ] && python3 "$A" paired "$C/d5s5.json" "$REF/fresh-s5.json"
[ -f "$C/d5s7.json" ] && python3 "$A" paired "$C/d5s7.json" "$REF/s7d3.json"
