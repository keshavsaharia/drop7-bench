#!/usr/bin/env bash
# The preregistered held-out SCREEN of EX-20260822-leaf-cmaes-d4s7-4f5f462a.
#
# Written and reviewed BEFORE the candidate existed (adversarial review
# runs/RUN-20260822T014331Z-1803c746/kimi-k3-review.md, section 2, gap 2), so
# that the single most safety-critical command of the experiment is not typed
# ad hoc.  It opens the held-out lease exactly once.
#
# Usage: screen.sh <evolution-run-dir> <screen-run-id>
#   <evolution-run-dir>/candidate-weights.txt must exist (the frozen CMA mean).
#
# Arms, in this order:
#   1. primary gate: depth 4, 5 strata, seeds 0xa52b0000+64, frozen vs candidate
#   2. secondary diagnostic: depth 4, 7 strata, the SAME seeds, frozen vs candidate
# The gate is evaluated by compare.py on arm 1 only.  Arm 2 is reported, never
# used to decide anything (experiment failureAction).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
cd "${ROOT}"
EVO="${1:?evolution run dir}"
SCREEN="${2:?screen run id}"
HELDOUT_START=0xa52b0000
HELDOUT_GAMES=64
THREADS="${THREADS:-30}"
B=./build/leaf-evolution/evaluate
OUT="runs/${SCREEN}"
mkdir -p "${OUT}"

test -s "${EVO}/candidate-weights.txt" || { echo "no frozen candidate at ${EVO}/candidate-weights.txt" >&2; exit 2; }
test -s "${EVO}/final.json" || { echo "evolution has not written final.json; the candidate is not frozen" >&2; exit 2; }
if [ -s "${OUT}/heldout-d4s5.json" ]; then
  echo "refusing: ${OUT}/heldout-d4s5.json exists; the held-out block is read exactly once" >&2
  exit 3
fi

# Population file: the frozen control first, then the candidate, both copied
# verbatim from their weight files so the artifact is self-describing.
python3 - "${EVO}" "${OUT}" <<'EOF'
import sys, pathlib
evo, out = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
names = ["open_columns","height_load","solid_cells","cracked_cells","numbered_cells","high_low_numbers",
         "direct_potential","latent_chain_potential","cracked_exposure","solid_exposure","adjacent_ones",
         "triple_twos","dead_low_numbers","covered_height_risk","low_number_height_risk","danger_height_squared",
         "rise_pressure","next_disc_vertical_options"]
def load(p):
    w = {}
    for line in p.read_text().splitlines():
        line = line.split('#')[0].strip()
        if not line: continue
        k, v = line.split()
        w[k] = v
    assert set(w) == set(names), f"{p}: terms {set(w) ^ set(names)}"
    return [w[n] for n in names]
frozen = load(evo / "weights-frozen.txt")
cand = load(evo / "candidate-weights.txt")
(out / "population.txt").write_text(
    "# held-out screen population: frozen control, then the frozen CMA-mean candidate\n"
    "frozen " + " ".join(frozen) + "\n"
    "candidate " + " ".join(cand) + "\n")
print("population written:", out / "population.txt")
EOF
sha256sum "${EVO}/candidate-weights.txt" "${OUT}/population.txt" | tee "${OUT}/inputs.sha256"

echo "== arm 1 (primary gate): d4 s5 seeds ${HELDOUT_START}+${HELDOUT_GAMES}"
"${B}" --population "${OUT}/population.txt" --output "${OUT}/heldout-d4s5.json" --label heldout-d4s5 \
       --seed-start "${HELDOUT_START}" --games "${HELDOUT_GAMES}" --threads "${THREADS}" \
       --depth 4 --chance-samples 5 --cache 60000 --max-moves 2000 --quiet 2> "${OUT}/heldout-d4s5.log"
/usr/bin/python3 approaches/lifetime-objective/leaf-evolution/compare.py "${OUT}/heldout-d4s5.json" \
       --candidate candidate --reference frozen --out "${OUT}/compare-d4s5.json" | tee "${OUT}/compare-d4s5.txt"

echo "== arm 2 (secondary diagnostic, not a gate): d4 s7 same seeds"
"${B}" --population "${OUT}/population.txt" --output "${OUT}/heldout-d4s7.json" --label heldout-d4s7 \
       --seed-start "${HELDOUT_START}" --games "${HELDOUT_GAMES}" --threads "${THREADS}" \
       --depth 4 --chance-samples 7 --cache 60000 --max-moves 2000 --quiet 2> "${OUT}/heldout-d4s7.log"
/usr/bin/python3 approaches/lifetime-objective/leaf-evolution/compare.py "${OUT}/heldout-d4s7.json" \
       --candidate candidate --reference frozen --out "${OUT}/compare-d4s7.json" | tee "${OUT}/compare-d4s7.txt"

sha256sum "${OUT}"/heldout-d4s5.json "${OUT}"/heldout-d4s7.json "${OUT}"/compare-d4s5.json "${OUT}"/compare-d4s7.json > "${OUT}/artifacts.sha256"
echo "screen complete: ${OUT}"
