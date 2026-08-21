# GPU/CPU note 03 — `torch.nn.Conv2d` on the CPU is nondeterministic on this host

**Status:** exploratory host defect, measured in this checkout on 2026-08-20 while
building the parity gate for
[`finding-08`](finding-08-learned-leaf.md). Reproducible in a few seconds.
**Nothing outside `approaches/lifetime-objective/learned-leaf/` and
`docs/exploratory/` was modified by this work.**

## Summary

On this machine, **two identical `torch.nn.Conv2d` forward passes over the same
input tensor, in `eval()` mode, single-threaded, with `OMP_NUM_THREADS`,
`MKL_NUM_THREADS` and `OPENBLAS_NUM_THREADS` all pinned to 1, produce different
results.** About 6.25% of output elements differ, by up to ~0.15 in absolute
terms on unit-variance inputs. `nn.GroupNorm`, `nn.Linear` and `torch.mm` are
bit-exact under the same conditions.

Disabling the oneDNN (MKL-DNN) path makes `Conv2d` bit-exact:

```python
with torch.backends.mkldnn.flags(enabled=False):
    ...                      # Conv2d now repeats exactly
```

This is a sibling of, but distinct from, the OpenBLAS SGEMM race in
[`gpu-02`](gpu-02-openblas-sgemm-race.md): that one is a *thread-count* bug in
the BLAS GEMM kernel and is silenced by `OPENBLAS_NUM_THREADS=1`; this one
survives every thread pin and lives in the convolution dispatch.

## Measurement

`torch 2.13.0+rocm7.1` in `.venv-rocm`, CPU tensors only, no GPU involved.
Four repeated forward passes of the same module over the same `(256, 128, 7, 7)`
input, comparing every later trial against the first:

| operation | max abs difference across repeats | fraction of elements differing |
| --- | ---: | ---: |
| `Conv2d(128, 128, 3, padding=1, bias=False)` | **0.146** | **6.25%** |
| `Conv2d(...)` with `mkldnn.flags(enabled=False)` | **0** | 0% |
| `GroupNorm(32, 128)` | 0 | 0% |
| `Linear(1568, 256)` | 0 | 0% |
| `torch.mm`, 256x1568 by 1568x256 | 0 | 0% |

6.25% is exactly 1 in 16, which is suggestive of a single lane of a 16-wide
vector operation, but no attempt was made here to identify the kernel.

At whole-model scale the effect is larger than the per-op number suggests,
because GroupNorm mixes every channel of a group: one perturbed value in the
stem convolution moves the group statistics and therefore **every** downstream
output. Repeated forward passes of the 3,006,543-parameter `SurvivalNet` over an
identical batch of 256 real board states differ in **100% of output elements**,
with a maximum absolute logit difference of **0.18 to 0.35** depending on the
run.

## Why it matters, concretely

It silently invalidated a parity gate. The first CPU comparison of the exported
C++ inference path against PyTorch reported a maximum absolute difference of
1.1e-5 across all heads; the identical command re-run twenty minutes later
reported failures on every head, a maximum relative error of 3.5e-2, and a
lifetime Pearson of 0.99998 instead of 0.9999999999995. Neither run was wrong
about the C++ code. The *reference* had moved.

The generalisation is the uncomfortable part: **any measurement in this
repository that compares something against a CPU PyTorch convolution — an export
check, a distillation target, a held-out metric, a teacher label — is measuring
that comparison plus an unknown perturbation of up to ~0.2 per logit**, unless
oneDNN was disabled or the work ran on the GPU. Training is not obviously
affected (the checkpoints here were trained on the GPU, and a training run
tolerates noise of this size as if it were extra regularisation), but any
*gate* is.

## What to do

1. For any CPU PyTorch reference used as a gate, wrap it in
   `torch.backends.mkldnn.flags(enabled=False)`.
2. **Prove the reference repeats before comparing anything to it.** Run it at
   least twice and require bit-identical output; refuse to gate against a
   moving target. `approaches/lifetime-objective/learned-leaf/parity_net.py`
   does this with `--determinism-trials` and exits non-zero on drift.
3. Prefer models without `Conv2d` where the deployment target is a CPU search
   anyway — an unrelated but convenient property of the NNUE-shaped student in
   `finding-08`, whose PyTorch reference repeats bit-for-bit with no flags at
   all.
4. Do not read this as a reason to distrust the GPU path. It was not tested here
   and the defect is specific to the CPU convolution dispatch.

## Reproduce

```python
import torch, torch.nn as nn
torch.set_num_threads(1); torch.manual_seed(0)
x = torch.randn(256, 128, 7, 7)
conv = nn.Conv2d(128, 128, 3, padding=1, bias=False).eval()
with torch.no_grad():
    a, b = conv(x), conv(x)
print((a - b).abs().max().item(), (a != b).float().mean().item())
with torch.no_grad(), torch.backends.mkldnn.flags(enabled=False):
    a, b = conv(x), conv(x)
print((a - b).abs().max().item(), (a != b).float().mean().item())
```

Environment: AMD Ryzen AI MAX+ 395 (Zen 5), 16 physical cores / 32 logical,
125 GiB unified RAM, `torch 2.13.0+rocm7.1`, oneDNN v3.12.0, MKL 2024.2,
Python 3.13. Every thread-count environment variable pinned to 1.

## Limitations

- One host, one torch build. Nothing here says whether the defect is in oneDNN,
  in the ROCm wheel's build of it, or in the CPU.
- The offending kernel was not identified and no upstream report was filed.
- Only `Conv2d` was tested among the convolution family; other layouts, dtypes
  and shapes were not swept, so "6.25%" describes one shape and should not be
  quoted as the defect's magnitude in general.
- GPU execution was not tested for the same property.
