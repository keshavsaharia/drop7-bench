# Reproducibility Guide

## Supported core toolchain

The core checks require:

- Node.js 22.6 or newer for built-in TypeScript type stripping;
- Python 3.10 or newer for the dependency-free research validator and machine
  profiler;
- Clang or GCC with C++20 and POSIX threading support; and
- GNU Make or a compatible `make` implementation.

The checked-in core has no npm runtime dependencies. Python/PyTorch experiments
are optional and use the packages listed in
`approaches/ntuple-rl/torch-ppo/requirements.txt`.

## Run the verified core

From the repository root:

```sh
npm test
make test-native
make parity
```

`npm test` runs the TypeScript rules and policy tests. `make test-native` builds
the reference native executables and runs their deterministic self-tests.
`make parity` compares seeded native traces with TypeScript traces, including
boards, scores, row rises, chain waves, and gray-disc reveals.

The combined command is:

```sh
make test
```

`make test` first validates the portable agent instructions, research schemas,
live record references, seed-lease invariants, and local documentation links.
For a read-only system/toolchain profile, run:

```sh
make research-doctor
```

Performance results retain that JSON profile. See the
[benchmark contract](benchmarks.md) and the
[Ryzen Halo guide](hardware/amd-ryzen-halo.md) before changing CPU, memory, or
GPU configuration.

Build products are written to `build/`, which is ignored by version control.

### Current verification snapshot

The reorganized checkout completed the combined command successfully:

- 122 TypeScript tests passed;
- the native gradient, n-tuple, n-tuple-search, fair-D3, and fair-D4 self-tests
  passed; and
- 256 seeded games matched exactly between the native and TypeScript engines
  across 6,852 moves.

This verifies the core mechanics and reference fixtures. It does not reproduce
the expensive complete-game score means in the historical ledger.

## Compile a standalone experiment

Every `.cpp` file under `approaches/` is designed as a standalone translation
unit or a wrapper that includes its reference translation unit. Compile a
corrected-score experiment through the Makefile:

```sh
make experiment SOURCE=approaches/fair-expectimax/reference/fair-only-depth4.cpp
./build/experiment --help
```

Or compile directly:

```sh
clang++ -O3 -std=c++20 -pthread -Wall -Wextra -Werror \
  approaches/tree-search/puct/puct.cpp -o build/puct
```

Do not attempt to link all C++ files together; almost every file defines its own
`main` function. `transition-reward-horizon.cpp` intentionally includes its
reference implementation, and `torch-env.cpp` switches between a self-test
executable and a Python module through compile-time macros.

### Historical 7,000-point source locks

A strict syntax pass over all 110 C++ entry points is expected to stop on a
subset of early experiments. In the current audit, 74 passed a bare strict
Clang syntax check, 35 stopped at historical 7,000-point locks, and the optional
PyTorch environment needed pybind11 headers. The locked sources contain
assertions such as:

```cpp
static_assert(kLevelBonus == 7'000);
```

The shared engine now correctly uses 17,000 points for the five-move mode. The
assertions intentionally prevent a Sequence-scored experiment from being
silently relabeled as corrected Hardcore evidence. Do not remove them merely to
make an archived executable build. Port the method as a new corrected-score
experiment, assign new development data, and record new results.

The optional `torch-env.cpp` also needs pybind11's include flags and is not
expected to pass a bare standard-library-only compiler invocation. The core
Make targets avoid both categories.

## Run TypeScript approaches

Use Node's type stripping directly. Examples:

```sh
npm run benchmark
node --experimental-strip-types approaches/tree-search/mcts/typescript.ts
node --experimental-strip-types approaches/fair-expectimax/fair-policy/tune.ts --help
```

Several historical lab scripts execute immediately when imported. Treat each
file as a command-line entry point unless its source contains an explicit main
guard.

## Python/PyTorch approach

Create an isolated environment and install the optional dependencies:

```sh
python3 -m venv .venv
. .venv/bin/activate
python3 -m pip install -r approaches/ntuple-rl/torch-ppo/requirements.txt
python3 approaches/ntuple-rl/torch-ppo/train.py --help
```

The trainer dynamically compiles `torch-env.cpp` through pybind11. Its current
linker flags include macOS's `-undefined dynamic_lookup`; Linux or Windows will
need a platform-specific extension build adjustment. Large PyTorch runs are not
part of the core verification command.

## Retained and generated artifacts

The repository retains only compact artifacts needed to understand or run an
approach:

- `artifacts/models/denoised-value/v1.bin`;
- the million-point validation protocol; and
- the optimistic phase n-tuple protocol and lane manifests.

Most historical checkpoints, JSONL corpora, and result bundles were written to
temporary storage and were not copied into this repository. A command in the
detailed ledger can therefore be syntactically correct while still requiring a
missing generated input. Check the experiment index and source defaults before
starting an expensive run.

## Reproducing a research claim

For a new claim, record:

1. source and model hashes;
2. compiler/interpreter versions and the exact command;
3. seed role and range;
4. search width, chance strata, horizon, work cap, and thread count;
5. per-game results, censor flags, and checksums;
6. peak memory and wall/process time; and
7. the fixed gate evaluated after the run.

Use the whole game as the confidence-interval unit. Never turn a previously
evaluated cohort into fresh confirmation evidence.

## Historical hash warning

The source tree was reorganized after the frozen protocols were written.
Includes, paths, filenames, and purpose comments changed, so historical SHA-256
values intentionally do not match current source bytes. The protocol files are
kept unchanged as historical evidence; they are not active authorization for a
new protected run.

Before resuming a source-locked experiment:

1. rerun all core tests and full native/TypeScript parity;
2. perform a semantic diff against the archived description;
3. create a new versioned protocol with current transitive-source hashes;
4. reserve new development/protected ranges as required; and
5. freeze the executable and configuration before evaluation.

See [provenance](provenance.md) for the evidence boundary.
