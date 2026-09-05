#!/usr/bin/env bash
# Sweeps every console route and social image against a running server and
# reports non-200s or unexpected image content types. Usage:
#   web/scripts/check-routes.sh http://localhost:3000
set -u
BASE="${1:-http://localhost:3000}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
routes=( / /play /compete /leaderboard /research /approach "/approach?view=family" /theories /experiments /results /log /learn /learn/vocabulary /learn/vocabulary/game /learn/vocabulary/search /learn/vocabulary/learning /learn/vocabulary/evidence /learn/concepts /learn/techniques /engine /diagnostics /src /docs /privacy /terms /robots.txt /sitemap.xml /docs/research/status /docs/methodology /docs/benchmarks /docs/strategies /docs/research/experiment-index )
images=( /opengraph-image /play/opengraph-image /compete/opengraph-image /leaderboard/opengraph-image /research/opengraph-image /approach/opengraph-image /theories/opengraph-image /experiments/opengraph-image /results/opengraph-image /log/opengraph-image /learn/vocabulary/opengraph-image /learn/vocabulary/game/opengraph-image /learn/vocabulary/search/opengraph-image /learn/vocabulary/learning/opengraph-image /learn/vocabulary/evidence/opengraph-image /learn/opengraph-image /learn/concepts/opengraph-image /learn/techniques/opengraph-image /engine/opengraph-image /diagnostics/opengraph-image /docs/opengraph-image /privacy/opengraph-image /terms/opengraph-image )
redirects=(
  "/approaches|$BASE/approach"
  "/approaches?view=family|$BASE/approach?view=family"
  "/approaches/fair-expectimax/reference|$BASE/approach/fair-expectimax/reference"
  "/engines|$BASE/engine"
  "/engines/native|$BASE/engine/native"
)
for t in expectimax heuristic-evaluation q-learning n-tuple nnue policy-gradient evolution mcts rollout-policy-iteration oracle-distillation risk-survival afterstate constructive-planning determinization; do routes+=("/approach/technique/$t"); done
for t in expectimax heuristic-evaluation q-learning n-tuple nnue policy-gradient evolution mcts rollout-policy-iteration oracle-distillation risk-survival afterstate constructive-planning determinization; do images+=("/approach/technique/$t/opengraph-image"); done
for p in "$ROOT"/web/content/learn/techniques/*.mdx; do if [ -f "$p" ]; then route="/learn/techniques/$(basename "$p" .mdx)"; routes+=("$route"); images+=("$route/opengraph-image"); fi; done
for r in "$ROOT"/research/results/RS-*.json; do if [ -f "$r" ]; then route="/results/$(basename "$r" .json)"; routes+=("$route"); images+=("$route/opengraph-image"); fi; done
for f in "$ROOT"/approaches/*/; do fam=$(basename "$f"); route="/approach/$fam"; routes+=("$route"); images+=("$route/opengraph-image"); for a in "$f"*/; do if [ -d "$a" ]; then route="/approach/$fam/$(basename "$a")"; routes+=("$route"); images+=("$route/opengraph-image"); fi; done; done
for t in "$ROOT"/research/theories/TH-*.json; do if [ -f "$t" ]; then route="/theories/$(basename "$t" .json)"; routes+=("$route"); images+=("$route/opengraph-image"); fi; done
for e in "$ROOT"/research/experiments/EX-*.json; do if [ -f "$e" ]; then route="/experiments/$(basename "$e" .json)"; routes+=("$route"); images+=("$route/opengraph-image"); fi; done
for l in "$ROOT"/web/content/learn/*.mdx; do if [ -f "$l" ]; then route="/learn/$(basename "$l" .mdx)"; routes+=("$route"); images+=("$route/opengraph-image"); fi; done
for c in "$ROOT"/web/content/learn/concepts/*.mdx; do if [ -f "$c" ]; then route="/learn/concepts/$(basename "$c" .mdx)"; routes+=("$route"); images+=("$route/opengraph-image"); fi; done
for l in "$ROOT"/web/content/log/*.mdx; do if [ -f "$l" ]; then route="/log/$(basename "$l" .mdx)"; routes+=("$route"); images+=("$route/opengraph-image"); fi; done
for e in typescript browser native fast scenario rust rust-classic gpu; do route="/engine/$e"; routes+=("$route"); images+=("$route/opengraph-image"); done
while IFS= read -r d; do slug=${d#"$ROOT/docs/"}; slug=${slug%.md}; route="/docs/$slug"; routes+=("$route"); images+=("/share/docs/$slug"); done < <(find "$ROOT/docs" -type f -name '*.md' | sort)
for replay in "$ROOT"/web/data/replays/*.json; do if [ -f "$replay" ]; then stem=$(basename "$replay" .json); policy=${stem%%--*}; round=${stem#*--}; route="/leaderboard/$policy/$round"; routes+=("$route"); images+=("$route/opengraph-image"); fi; done
bad=0; total=0
for r in "${routes[@]}"; do
  total=$((total+1))
  code=$(curl -s -o /dev/null -w "%{http_code}" "$BASE$r")
  if [ "$code" != "200" ]; then echo "$code $r"; bad=$((bad+1)); fi
done
for r in "${images[@]}"; do
  total=$((total+1))
  result=$(curl -s -o /dev/null -w "%{http_code} %{content_type}" "$BASE$r")
  if [[ "$result" != "200 image/png" && "$result" != "200 image/svg+xml" && "$result" != "200 image/svg+xml; charset=utf-8" ]]; then echo "$result $r"; bad=$((bad+1)); fi
done
for redirect in "${redirects[@]}"; do
  total=$((total+1))
  source=${redirect%%|*}
  expected=${redirect#*|}
  result=$(curl -s -o /dev/null -w "%{http_code} %{redirect_url}" "$BASE$source")
  if [ "$result" != "308 $expected" ]; then echo "$result $source (expected 308 $expected)"; bad=$((bad+1)); fi
done
sitemap=$(curl -fsS "$BASE/sitemap.xml") || { echo "sitemap.xml could not be read"; bad=$((bad+1)); sitemap=""; }
if [[ "$sitemap" != *"/approach</loc>"* || "$sitemap" != *"/engine</loc>"* ]]; then
  echo "sitemap.xml is missing a singular canonical collection route"
  bad=$((bad+1))
fi
if [[ "$sitemap" == *"/approaches"* || "$sitemap" == *"/engines"* ]]; then
  echo "sitemap.xml contains a legacy plural route"
  bad=$((bad+1))
fi
echo "$total pages, social images and redirects checked, $bad failures"
[ "$bad" -eq 0 ]
