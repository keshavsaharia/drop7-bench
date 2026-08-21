# suite-validation

Validates the scenario benchmark of
[`design-01`](../../../docs/exploratory/design-01-benchmark-suite.md) and
[`finding-02`](../../../docs/exploratory/finding-02-scenario-benchmark.md)
against whole-game strength, and carries an instrument for mining the structure
of the achievable clear rate that
[`finding-06`](../../../docs/exploratory/finding-06-flow-ceiling.md) and
[`finding-07`](../../../docs/exploratory/finding-07-fair-planning-ceiling.md)
identified as the binding quantity.

Result: [`finding-10`](../../../docs/exploratory/finding-10-suite-validation.md).
**Check 1 failed** (Spearman +0.633 against a 0.70 threshold). The structure
probe was subsequently authorized by the coordinator — it does not consume the
suite — and its result is Addendum A of the same document: the frozen leaf's
feature basis is nearly sufficient and its **weights** are the problem
(reweighting the 19 moves held-out R² from 0.396 to 0.734, against a 0.753
ceiling over 53 candidate properties). Addendum B recommends **not** building a
suite v2.
Preregistration: [`PREREGISTRATION.md`](PREREGISTRATION.md).

**No file outside this directory, `build/suite-validation/`,
`runs/RUN-SUITE-*/` and `docs/exploratory/` was created or modified.** Every
frozen source is consumed unmodified; two of them end in a real `int main` and
are compiled from build-tree copies whose only changed line is the entry point,
with the one-line diff enforced by `build.sh`.

| File | What it is |
| --- | --- |
| `policies.hpp` | The nine-policy comparator set and the seed-lease guard. The six fair arms are `risk-calibration/search.cpp`'s `ParameterizedSearch`, unmodified |
| `posmode.hpp` | Position mode: retape/relatent primitives and the scenario play loop |
| `posmode.cpp` | Position-mode evaluation CLI and its CHECK gates |
| `features.hpp` | 53 candidate structural properties of a position, public-state-only and reflection-invariant |
| `structure.cpp` | The structure probe: exact achievable-clear labels plus candidate and frozen-leaf features |
| `split.py` | Check 2: the development/sealed split manifest with content hashes |
| `stats.py` | Pure-Python statistics (no numpy in this interpreter) |
| `analyze.py` | Check 1 and the discriminating-power analysis |
| `analyze_structure.py` | The structural regression on a whole-position three-way split |
| `leaf_weights.py` | The frozen leaf's weight direction against the fitted one |
| `data/suite-h9-v1-split-v1.json` | The frozen split manifest |

## Build and gates

```sh
./approaches/lifetime-objective/suite-validation/build.sh

# the scenario engine's own gate, from finding-02, is a prerequisite
./approaches/lifetime-objective/scenario/build.sh
./build/scenario/scenario-parity --seeds 4096

# this work's gates
./build/suite-validation/posmode --self-test \
    --suite approaches/lifetime-objective/scenario/data/suite-h9-v1.jsonl --tapes 4
./build/suite-validation/structure --self-test
```

## Run

```sh
RID=RUN-SUITE-9c41ab7e2d10

# Check 1 - position mode
./build/suite-validation/posmode \
    --suite approaches/lifetime-objective/scenario/data/suite-h9-v1.jsonl \
    --horizon 9 --tapes 4 --threads 8 --jsonl runs/$RID/posmode-h9-k4.jsonl
python3 approaches/lifetime-objective/suite-validation/analyze.py \
    runs/$RID/posmode-h9-k4.jsonl \
    --split approaches/lifetime-objective/suite-validation/data/suite-h9-v1-split-v1.json

# Check 2 - the split manifest
python3 approaches/lifetime-objective/suite-validation/split.py \
    approaches/lifetime-objective/scenario/data/suite-h9-v1.jsonl \
    approaches/lifetime-objective/suite-validation/data/suite-h9-v1-split-v1.json

# the post-hoc horizon diagnostic (11 minutes; omits the two depth-4 arms)
./build/suite-validation/posmode \
    --suite approaches/lifetime-objective/scenario/data/suite-h9-v1.jsonl \
    --horizon 25 --tapes 4 --threads 8 \
    --policies d2s5,d2s7,d3s5,d3s7,center-first,random-legal,lowest-column \
    --jsonl runs/$RID/posmode-h25-k4.jsonl

# Task 2 - the structure probe (1,024 positions, 1,198 s on 8 threads)
./build/suite-validation/structure --fair 384 --weak 192 --synthetic 448 \
    --horizon 8 --completions 4 --threads 8 --time-limit 20 \
    --csv runs/$RID/structure-h8-j4.csv
python3 approaches/lifetime-objective/suite-validation/analyze_structure.py \
    runs/$RID/structure-h8-j4.csv
python3 approaches/lifetime-objective/suite-validation/leaf_weights.py \
    runs/$RID/structure-h8-j4.csv
```

## Seed lease

`SEEDLEASE-A52-SUITE` = `0xa5258000`–`0xa525bfff`, a sub-range of the
`SEEDLEASE-A52` reserve in
[`lease-map.md`](../../../docs/exploratory/lease-map.md). Role: **exploratory
development diagnostic**; once read these seeds are development data
permanently. `posmode` and `structure` both refuse to draw outside it.
