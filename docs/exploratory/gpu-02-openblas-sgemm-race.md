# Upstream bug: multithreaded OpenBLAS SGEMM returns wrong results on Zen 5

**Status:** reproduced and characterised here; not yet reported upstream.
**Found:** 2026-08-20, incidentally, while enabling ROCm GPU training (see
`gpu-01-rocm-enablement.md`).
**Severity for this repository:** high. It silently corrupts `numpy` float32
linear algebra on the research machine, with no error and no warning.
**Mitigation already in place:** `OPENBLAS_NUM_THREADS=1` is exported by
`approaches/lifetime-objective/gpu/activate.sh`, and
`bench.py --correctness` now gates on a host-side SGEMM sanity check.

---

## 1. Summary

On this machine, `numpy`'s float32 matrix multiply returns **wrong answers** for
roughly 0.1%-1% of output elements once OpenBLAS is allowed four or more
threads. The wrong values are not a precision artifact. They are ~10^5 times
larger than legitimate float32 rounding, and they **differ from run to run for
byte-identical inputs**, which makes this a data race rather than a numerical
accuracy question.

float64 (`DGEMM`) never reproduced it. One and two threads never reproduced it.
The same multiplication performed by a *different* BLAS implementation (oneDNN,
via PyTorch) on the same CPU, in the same process, interleaved with the failing
calls, was correct every single time — which is the observation that rules out
faulty silicon.

Nothing in the failure is visible to a caller. There is no exception, no
warning, and no NaN. A training run would simply learn from slightly wrong
numbers.

## 2. Environment

| Component | Value |
|---|---|
| CPU | AMD Ryzen AI MAX+ 395 w/ Radeon 8060S ("Strix Halo", Zen 5 / znver5) |
| Topology | 16 physical cores / 32 threads, 1 socket |
| Relevant ISA | `avx512f avx512dq avx512bw avx512vl avx512_bf16 avx512_vnni ...` |
| RAM | 125 GiB unified |
| OS | Debian, glibc 2.41, kernel `6.18.35+rex+2-amd64` |
| Python | 3.13.5 |
| numpy | 2.5.2 (also reproduced with 2.4.3) |
| BLAS | `scipy-openblas 0.3.34.0.0` (the OpenBLAS bundled in the numpy wheel) |
| OpenBLAS config | `OpenBLAS 0.3.34.0.0 USE64BITINT DYNAMIC_ARCH NO_AFFINITY SkylakeX MAX_THREADS=64` |
| Kernel selected | **`SkylakeX`** (confirmed via `OPENBLAS_VERBOSE=2` → `Core: SkylakeX`) |

The selected kernel is the key environmental detail. OpenBLAS 0.3.34 is a
`DYNAMIC_ARCH` build with no `znver5` target, so on this CPU it dispatches to
the **Intel SkylakeX AVX-512 kernel**. The bug is in that path as exercised by
this processor.

## 3. What the failure looks like

`A @ B` for `A, B ~ N(0,1)` float32, `n = 1024`, 16 threads:

```
run 0: max_err=2.144e+01  bad=0.479%  md5=c66db0dc0c38
run 1: max_err=2.271e+01  bad=0.480%  md5=ac58820c987a
run 2: max_err=2.447e+01  bad=0.479%  md5=30dfce2e29c3
run 3: max_err=2.403e+01  bad=0.476%  md5=548980014a6a
run 4: max_err=2.002e+01  bad=0.481%  md5=d13a3c5ee25d
run 5: max_err=2.146e+01  bad=0.481%  md5=54056f2d251e
   -> 6 distinct results from 6 identical runs
```

Same inputs, same thread count, same process — six different answers. For
reference, correct float32 rounding error for this problem is about `eps32 * k`
≈ `1.2e-4`. The observed errors are `~2e+1`, i.e. **five orders of magnitude
too large**, and they land on whole blocks of the output rather than being
spread out. That pattern is what you would expect from a thread reading a
result tile before another thread has finished writing it, or from a packing
buffer being reused while still live.

Meanwhile `DGEMM` on the same data is bit-reproducible across runs.

## 4. Envelope

`n x n` float32 GEMM, 5 repeats per cell. "corrupt" counts elements with
absolute error > 1.0; "distinct" counts unique results across the 5 identical
repeats (any value > 1 is by itself proof of nondeterminism).

| threads | n=512 | n=1024 | n=2048 | n=4096 |
|---|---|---|---|---|
| 1 | clean, 1/5 | clean, 1/5 | clean, 1/5 | clean, 1/5 |
| 2 | clean, 1/5 | clean, 1/5 | clean, 1/5 | clean, 1/5 |
| 4 | clean, 1/5 | clean, 1/5 | clean, 1/5 | 0.030%, **2/5** |
| 8 | clean, 1/5 | clean, 1/5 | 0.809%, **5/5** | 0.377%, **3/5** |
| 16 | 0.252%, **5/5** | 0.328%, **5/5** | 0.155%, **3/5** | clean, 1/5 |
| 32 | 0.366%, **5/5** | 1.148%, **5/5** | 0.121%, **3/5** | 0.175%, **5/5** |

Maximum absolute errors ranged from `1.4e+01` to `4.4e+01`.

The failure is **intermittent per process invocation** — note that 16 threads at
`n=4096` came out clean in this particular sweep while 32 threads at the same
size did not. Do not read the blank cells as "safe sizes"; a later repetition of
the identical sweep produced a different pattern of hits. The only
configurations that never failed across every experiment run were **1 and 2
threads**.

Repeat-count sensitivity, `n=1024`, 40 trials per configuration:

| configuration | trials corrupted | max error |
|---|---|---|
| numpy / OpenBLAS, 32 threads | **40 / 40** | 2.399e+01 |
| numpy / OpenBLAS, 16 threads | **13 / 40** | 2.636e+01 |
| torch / oneDNN, 16 threads | **0 / 40** | — |

## 5. Why this is a software bug and not a broken CPU

This distinction matters enormously — one conclusion is "file a bug", the other
is "stop trusting this machine" — so it was tested directly rather than assumed.

1. **A second BLAS on the same silicon is clean.** The oneDNN row above is the
   control arm. PyTorch's CPU `float32` GEMM ran 40/40 correct at 16 threads,
   interleaved in the same process with the OpenBLAS calls that were failing
   13/40 at that moment. Silent hardware corruption would not politely confine
   itself to one library's kernels.
2. **Single-threaded is perfect.** Hardware-level SDC from AVX-512 stress would
   not vanish entirely at 1-2 threads while appearing at 4+.
3. **The error scale is structural, not physical.** Bit flips from marginal
   hardware produce scattered single-element damage of arbitrary magnitude. What
   we see is coherent blocks of output at exactly the magnitude of a partially
   accumulated GEMM tile.
4. **float64 is unaffected**, despite exercising the same cores, the same
   threading layer, and more memory bandwidth.

Points 1 and 2 together are, in my view, conclusive.

## 6. Reproducer

`approaches/lifetime-objective/gpu/openblas_sgemm_race.py` is self-contained and
needs only numpy (torch is optional and, when present, adds the control arm).

```bash
# most reliable trigger on this machine
OPENBLAS_NUM_THREADS=32 python approaches/lifetime-objective/gpu/openblas_sgemm_race.py

# thread x size envelope (re-run per thread count: OpenBLAS reads the
# variable once at load time)
for t in 1 2 4 8 16 32; do
  OPENBLAS_NUM_THREADS=$t python approaches/lifetime-objective/gpu/openblas_sgemm_race.py --sweep
done
```

It exits non-zero when it reproduces. It reports the OpenBLAS config string and
selected core name so the output is self-describing.

## 7. Impact on this repository

- **Any `numpy` float32 `@`, `dot`, `matmul`, `einsum` with a BLAS path, or
  `scipy.linalg` call on this machine was suspect** whenever OpenBLAS had >= 4
  threads. That is the default: OpenBLAS sizes its pool from the core count.
- It is *not* limited to the GPU work. It applies to any float32 linear algebra
  anywhere in the research program that goes through numpy on this host.
- PyTorch on the `pytorch.org` ROCm wheel is **not** affected for its own
  tensor ops — it uses oneDNN — but `numpy` inside the same process still is.
- The AMD "TheRock" `torch` wheels **are** affected for torch ops too, because
  they link `librocm-openblas.so.0` for host BLAS.
- It cost real debugging time indirectly: it initially made the CPU look far
  less numerically accurate than the GPU in the conv-net correctness checks
  (CPU fp32 gradient error `~1e-2` vs GPU `~4e-8`). That gap was not a genuine
  CPU/GPU precision difference — it was this bug. With
  `OPENBLAS_NUM_THREADS=1` the two agree to `~1e-7`. Any earlier note in this
  repo claiming the GPU is intrinsically more accurate than the CPU for
  convolutions should be read in that light.

### Mitigation

`OPENBLAS_NUM_THREADS=1` is exported by `activate.sh`. This costs essentially
nothing here: PyTorch does its CPU math through oneDNN, which remains threaded
across all cores and is unaffected. It only removes threading from numpy's own
BLAS calls.

`bench.py --correctness` now includes a `host CPU fp32 GEMM trustworthy` check
so that a regression, or a shell that forgot the variable, is caught rather than
silently believed.

## 8. Confidence, and what is not yet established

What is solid: the corruption is real, reproducible on demand, thread-count
dependent, nondeterministic, confined to float32, and confined to OpenBLAS
rather than the CPU.

What is **not** yet established, and would be needed for a high-quality upstream
report:

1. **Whether upstream OpenBLAS is at fault, or this particular build.** Only the
   `scipy-openblas 0.3.34.0.0` wheel build has been tested. A distribution
   OpenBLAS, and a from-source build at the same version, should be tested
   before blaming upstream source.
2. **Whether it is version-specific.** 0.3.34 only. Worth testing 0.3.35+ and
   `develop`, and worth searching the OpenBLAS issue tracker for existing
   Zen 5 / SkylakeX-dispatch reports before opening a new one.
3. **Whether it is Zen-5-specific or affects genuine SkylakeX hardware too.**
   The `SkylakeX` kernel running on `znver5` is the obvious suspect, but that is
   currently an inference from the dispatch string, not a tested claim. Testing
   the same wheel on a real Intel Skylake-X/Ice Lake part would separate
   "SkylakeX kernel is racy" from "SkylakeX kernel is racy *on Zen 5*".
4. **Which kernel exactly.** `OPENBLAS_CORETYPE=Haswell` was tried in order to
   A/B the dispatch, but it segfaults in this wheel, so the kernel could not be
   swapped to confirm the SkylakeX path is the culprit. Building OpenBLAS with
   `TARGET=HASWELL` and re-testing is the clean way to settle it.
5. **The mechanism.** "Race in threaded SGEMM" is inferred from the symptom
   shape. Narrowing it to a specific buffer would need a threaded build under
   a race detector, or bisection of `GEMM_P/Q/R` blocking parameters.

A reasonable next step, if someone wants to pursue this, is item 1 and item 2
together — they are cheap, and they determine whether this is worth an upstream
issue at all or is already fixed.

## 9. Why this is an interesting thread to pull

Two reasons beyond the immediate fix.

The first is that new CPU microarchitectures reach users well before numerical
libraries grow explicit support for them, and `DYNAMIC_ARCH` failure is *silent
by construction*: it picks the nearest known kernel and proceeds. Zen 5 is
recent enough that "falls back to the Intel AVX-512 path" is the expected
behaviour, not an anomaly. If that fallback is subtly racy, every float32
workload on the machine inherits it with no diagnostic. That is a systematic
hazard for any lab standing up new hardware, not a quirk of this box.

The second is methodological, and it is the part worth internalising here. The
bug was found only because a GPU correctness check compared against a CPU
reference and the *reference* was what failed. The natural reading of that
result — "the GPU disagrees with the CPU, so the GPU is wrong" — was exactly
backwards. Cross-checking two implementations tells you they disagree; it does
not tell you which one to believe. What resolved it was introducing a third,
higher-precision arm (float64) that neither implementation could be checked
against circularly. Given how much of this repository's evidence rests on
comparing a candidate against a baseline, "the baseline is the broken one" is a
failure mode worth keeping on the table.
