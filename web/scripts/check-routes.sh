#!/usr/bin/env bash
# Sweeps every console route (static + one per family/approach/theory/experiment/
# learn page) against a running server and reports non-200s. Usage:
#   web/scripts/check-routes.sh http://localhost:3000
set -u
BASE="${1:-http://localhost:3000}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
routes=( / /compete /approaches /theories /experiments /learn /learn/concepts /leaderboard /docs/research/status /docs/methodology /docs/benchmarks /docs/strategies /docs/research/experiment-index )
for f in "$ROOT"/approaches/*/; do fam=$(basename "$f"); routes+=("/approaches/$fam"); for a in "$f"*/; do [ -d "$a" ] && routes+=("/approaches/$fam/$(basename "$a")"); done; done
for t in "$ROOT"/research/theories/TH-*.json; do routes+=("/theories/$(basename "$t" .json)"); done
for e in "$ROOT"/research/experiments/EX-*.json; do routes+=("/experiments/$(basename "$e" .json)"); done
for l in "$ROOT"/web/content/learn/*.mdx; do routes+=("/learn/$(basename "$l" .mdx)"); done
for c in "$ROOT"/web/content/learn/concepts/*.mdx; do routes+=("/learn/concepts/$(basename "$c" .mdx)"); done
bad=0; total=0
for r in "${routes[@]}"; do
  total=$((total+1))
  code=$(curl -s -o /dev/null -w "%{http_code}" "$BASE$r")
  if [ "$code" != "200" ]; then echo "$code $r"; bad=$((bad+1)); fi
done
echo "$total routes checked, $bad not 200"
[ "$bad" -eq 0 ]
