# GPU enablement: PyTorch on ROCm / gfx1151 (Radeon 8060S, Strix Halo)

**Question:** can we train Drop7 policy/value networks on this machine's
integrated GPU, and is it worth doing versus the 32-thread CPU?

**Answer:** yes, it works and it computes correct results — but only after
working around two defects in the PyTorch ROCm wheel. For the network size we
actually need, the GPU wins by 1.25x-5x depending on batch size, and the low
end of that range is small enough that the CPU remains a legitimate fallback.

**Scope note:** this is an infrastructure/measurement package. No gameplay was
run, no seed was opened, and no policy claim is made.

> **Load caveat, stated up front.** Every throughput number here was measured on
> a *heavily contended* host — other agents held 20-50 of the 32 logical CPUs
> for the entire session. Figures are reported as **best-of-N** (the minimum
> observed time, since contention can only slow a run down), and every table
> carries the host load average observed while it was taken. They are **lower
> bounds**. Re-run on an idle machine before promoting any performance claim.

---

## 1. Environment

| Item | Value |
|---|---|
| GPU | Radeon 8060S Graphics, `gfx1151` (RDNA 3.5), 20 CUs, 2 MB L2, max 2900 MHz |
| ROCm | 7.13.0 at `/opt/rocm`; HIP 7.13.99004; `hipcc`, `rocm-smi`, `rocprofv3` present |
| Kernel | `6.18.35+rex+2-amd64`, glibc 2.41 |
| CPU | Ryzen AI MAX+ 395, 16 physical cores / 32 threads |
| Memory | **94.94 GiB unified** — see below |
| Interpreters | `/usr/bin/python3` → `python3.13` (3.13.5); linuxbrew `python3` is 3.14.6 |
| Disk | 1.6 TB free |

### Memory is one pool, not two

This is an APU with no dedicated VRAM. Do not report GPU memory and host RAM as
separate capacity.

- `rocm-smi` reports "VRAM Total 512 MiB". That is only the BIOS carveout and is
  **misleading** — it is not the limit on what a training job can allocate.
- The real figure is the GTT/global pool: `mem_info_gtt_total` =
  101,938,552,832 B and `torch.cuda.get_device_properties(0).total_memory` =
  97,216 MB, i.e. **94.94 GiB**, shared with the 125 GiB of system RAM.
- `bench.py` therefore reports peak allocation as a **percentage of the shared
  pool**. The largest configuration measured (128 channels, batch 8192) used
  5.52 GiB — **5.81%** of the pool. Memory is nowhere near being the constraint.

### Device permissions

`/dev/kfd` and `/dev/dri/renderD128` are group `render`, and the user is **not**
in that group. Access currently works only via a `systemd-logind` seat ACL
(`user:keshav:rw-`). That ACL is granted to the local seat session; a plain SSH
or a systemd service would likely **not** get it. *Recommendation (not applied —
needs a re-login to take effect): `sudo usermod -aG render,video keshav`.*

---

## 2. Install: what actually worked

### Recommended stack — `.venv-rocm`

```bash
/usr/bin/python3.13 -m venv .venv-rocm
.venv-rocm/bin/python -m pip install --upgrade pip
.venv-rocm/bin/python -m pip install \
    --index-url https://download.pytorch.org/whl/rocm7.1 torch torchvision
```

Installs `torch 2.13.0+rocm7.1`, `torchvision 0.28.0+rocm7.1`,
`triton-rocm 3.7.1`. Took 3m37s.

Python 3.13 was chosen over the 3.14 on `PATH` purely for ecosystem maturity —
`cp314` wheels do exist on this index, so 3.14 was not a blocker.

**`gfx1151` needs no override.** The wheel ships it natively:

```
torch.cuda.get_arch_list() = ['gfx900','gfx906','gfx908','gfx90a','gfx942',
                              'gfx1030','gfx1100','gfx1101','gfx1102','gfx1103',
                              'gfx1200','gfx1201','gfx950','gfx1150','gfx1151']
```

`HSA_OVERRIDE_GFX_VERSION` is **not set** and should not be. It was tested at
both `11.0.0` and `11.5.1`; it fixes nothing here and only risks running
mismatched code objects.

### Alternative stack — `.venv-rocm-therock`

AMD publishes wheels built specifically for this target, against the same ROCm
7.13 as the system install:

```bash
/usr/bin/python3.13 -m venv .venv-rocm-therock
.venv-rocm-therock/bin/python -m pip install \
    --index-url https://rocm.nightlies.amd.com/v2/gfx1151/ \
    "torch==2.9.1+rocm7.13.0a20260501" torchvision
```

Installs `torch 2.9.1+rocm7.13.0a20260501`, `arch_list == ['gfx1151']`, HIP
7.13.26173. Took 1m45s. **Needs no `LD_PRELOAD` and BatchNorm training works.**
Its drawbacks are an older torch (2.9.1) and that it links AMD's bundled
OpenBLAS for host BLAS, which drags in the corruption bug of section 6.

### Things that were tried and failed

| Attempt | Outcome |
|---|---|
| `pip install ... rocm7.13` index | HTTP 403 — no such index; 6.3 / 6.4 / 7.0 / 7.1 exist |
| `HSA_OVERRIDE_GFX_VERSION=11.5.1` / `11.0.0` | No effect on the segfault |
| `HSA_ENABLE_SDMA=0`, `GPU_MAX_HW_QUEUES=1`, `HSA_XNACK=1` | No effect |
| `MIOPEN_DEBUG_GCN_ASM_KERNELS=0` | No effect on the BatchNorm failure |
| `LD_PRELOAD` the system **MIOpen** | **Actively harmful** — see 3.2 |
| `OPENBLAS_CORETYPE=Haswell` | Segfaults in the scipy-openblas wheel |

---

## 3. Two real bugs in the wheel, and how they were diagnosed

### 3.1 Bundled ROCr segfaults on the first GPU operation

Out of the box, **every** GPU operation died:

```python
>>> torch.arange(4, dtype=torch.float32).cuda()
Segmentation fault (exit 139)
```

`AMD_LOG_LEVEL=3` put the crash precisely at HSA hardware-queue creation:

```
hipMemcpyWithStream ( 0x7effb2000000, ..., hipMemcpyHostToDevice, ... )
rocdevice.cpp:2871: Number of allocated hardware queues with low priority: 0, ...
Fatal Python error: Segmentation fault
```

To separate "wheel is broken" from "GPU/driver is broken", a native HIP
vector-add was compiled against the *system* ROCm and run:

```bash
/opt/rocm/bin/hipcc --offload-arch=gfx1151 -O2 vadd.cpp -o vadd && ./vadd
# malloc ok / memcpy ok / launch/sync: no error / max abs err = 0
```

The hardware and driver are fine. The fault is in the ROCr runtime **bundled
inside the wheel**, which cannot create a queue against this kernel's KFD.

**Fix:** preload the system ROCr. This is safe specifically because ROCr is a
*leaf* library — it pulls in no other ROCm component, so interposing it does not
duplicate the HIP runtime.

```bash
export LD_PRELOAD=/opt/rocm/lib/libhsa-runtime64.so.1
```

### 3.2 Bundled MIOpen cannot build its BatchNorm training kernel

With the GPU alive, every *training* step through a BatchNorm layer failed:

```
<inline asm>:17:20: error: not a valid operand.
v_add_f32 v2 v2 v2 row_bcast:31 row_mask:0xc
MIOpen Error: ... Code object build failed. Source: MIOpenBatchNormFwdTrainSpatial.cl
RuntimeError: miopenStatusUnknownError
```

`row_bcast` DPP is a **GFX9-only** encoding; MIOpen picked a GCN-era solver for
gfx1151 and the assembler rejected it. This only affects `train()` mode —
`eval()` dispatches elsewhere and works, which makes the bug very easy to miss.
The original correctness suite passed precisely because it used `eval()`.

**The obvious fix is a trap.** Preloading the system MIOpen *does* make
BatchNorm work, and then corrupts the process:

```
malloc_consolidate(): invalid chunk size
```

MIOpen is not a leaf library. Preloading it pulls in the system
`libamdhip64.so.7` **alongside** the wheel's own, giving the process two HIP
runtimes and two device contexts. `/proc/self/maps` confirms both. It survives
simple tests and then segfaults nondeterministically a few training steps in.
This is documented in `activate.sh` so nobody re-discovers it.

**Fix actually used:** avoid MIOpen for normalisation. `bench.py --norm group`
(the default) uses `GroupNorm`, a native PyTorch kernel. This is also defensible
on the merits for self-play/RL, where BatchNorm's running statistics drift
between the acting and learning distributions and couple samples within a batch.

If BatchNorm is genuinely required, use `.venv-rocm-therock`, where it works.

---

## 4. Correctness — the GPU computes the right answers

A target that imports but computes garbage is the failure mode worth fearing, so
these are scored against a **float64 CPU reference**, not against CPU fp32. That
matters: CPU fp32 turned out to be the untrustworthy side (section 6).

Run: `bench.py --correctness` on `.venv-rocm`.

| Check | Result |
|---|---|
| host CPU fp32 GEMM trustworthy | PASS — 0.000% of elements off by >1e-2 |
| h2d/d2h roundtrip | PASS — max diff 0 |
| 1M-element sum | PASS — rel 7.83e-07 |
| **4096x4096 fp32 matmul vs float64** | **PASS — max abs diff 1.157519e-03**, max&#124;C&#124; = 335.39, **rel 3.451e-06** |
| 1024x1024 bf16 matmul vs float64 | PASS — rel 3.032e-03 |
| MLP fwd+bwd gradients finite | PASS — all 6 tensors finite |
| MLP gradients vs float64 | PASS — GPU max grad err 1.654e-08 |
| Drop7 ConvNet forward vs float64 | PASS — max logit err 1.132e-06 |
| Drop7 ConvNet gradients vs float64 | PASS — max grad err 3.278e-07 |
| BatchNorm TRAIN-mode fwd+bwd | **WARN** — `miopenStatusUnknownError` (section 3.2) |
| 5 AdamW steps change weights, stay finite | PASS — max delta-w 5.014e-03, all finite |

**The headline number, in the form originally asked for:** 4096x4096 fp32
matmul, **GPU vs CPU max absolute difference = 1.220703e-03** (relative to
max&#124;C&#124; = 342.5, i.e. 3.56e-06). The GPU-vs-float64 figure above is
1.157519e-03. The two agree, which is the actual point: the GPU is not merely
*close to the CPU*, both are close to the truth.

**Stability soak.** 200 optimiser steps, 128 channels, batch 1024, GroupNorm:
all parameters finite; loss fell 2.948 → 0.003 on a fixed batch (i.e. the
network demonstrably learns, not just runs).

---

## 5. Benchmarks

Reported as **best-of-N**; `med ms` is shown alongside so the best/median gap
exposes how contended the host was.

### 5.1 Matmul throughput

Best observed across the session (`--matmul`, `.venv-rocm`):

| device | dtype | N | best ms | TFLOP/s (best) | peak alloc |
|---|---|---|---|---|---|
| cuda | fp32 | 1024 | 1.947 | 1.10 | 44.00 MiB |
| cuda | fp32 | 2048 | 12.375 | 1.39 | 80.00 MiB |
| cuda | fp32 | 4096 | 98.658 | 1.39 | 224.00 MiB |
| cuda | fp32 | 8192 | 922.008 | 1.19 | 800.00 MiB |
| cuda | bf16 | 1024 | 0.139 | 15.41 | 40.00 MiB |
| cuda | bf16 | 2048 | 1.133 | 15.17 | 64.00 MiB |
| cuda | bf16 | 4096 | 8.785 | 15.64 | 160.00 MiB |
| cuda | bf16 | 8192 | 73.718 | 14.92 | 544.00 MiB |
| cpu | fp32 | 1024 | 15.956 | 0.13 | — |
| cpu | fp32 | 2048 | 57.995 | 0.30 | — |
| cpu | fp32 | 4096 | 308.162 | 0.45 | — |
| cpu | fp32 | 8192 | 2032.080 | 0.54 | — |
| cpu | bf16 | 1024 | 7.393 | 0.29 | — |
| cpu | bf16 | 2048 | 11.549 | 1.49 | — |
| cpu | bf16 | 4096 | 88.309 | 1.56 | — |
| cpu | bf16 | 8192 | 570.675 | 1.93 | — |

Host load during this table: 29.8–34.6.

**In the least-contended window of the whole session** the same GPU sweep
reached **27.76 TFLOP/s bf16** (n=1024) and **2.60 TFLOP/s fp32** (n=4096) —
roughly 1.8x the numbers above. That is the clearest single illustration of how
much the shared host is costing these measurements, and it means **bf16 peak on
this iGPU is at least ~28 TFLOP/s.**

Two structural observations that are not contention artifacts:

- **bf16 is ~11-14x faster than fp32.** RDNA 3.5 has WMMA matrix cores for
  bf16/fp16 but not for fp32, and rocBLAS's fp32 SGEMM does not use them. Any
  serious training on this GPU should be bf16.
- fp32 at ~1.4 TFLOP/s is only ~5% of the ~30 TFLOP/s theoretical fp32 peak.
  This path is not well optimised on gfx1151.

### 5.2 Training throughput — the network we actually need

Architecture: input `(N, 12, 7, 7)` — empty, discs 1-7, solid cover, cracked
cover, plus a next-disc plane and a moves-until-rise plane — stem, 6 residual
blocks, and two heads (7-way column logits + 1 scalar). GroupNorm. AdamW.

#### 128 channels — 2,006,888 parameters (host load 10.6–33.7, mean 20.6)

| device | batch | best ms | samples/sec | peak alloc | % shared pool |
|---|---|---|---|---|---|
| cuda | 256 | 73.54 | **3,481** | 261.64 MiB | 0.27% |
| cuda | 1,024 | 200.27 | **5,113** | 783.10 MiB | 0.81% |
| cuda | 4,096 | 651.88 | **6,283** | 2.80 GiB | 2.95% |
| cuda | 8,192 | 1331.98 | **6,150** | 5.52 GiB | 5.81% |
| cpu | 256 | 150.82 | 1,697 | — | — |
| cpu | 1,024 | 604.72 | 1,693 | — | — |
| cpu | 4,096 | 2924.81 | 1,400 | — | — |
| cpu | 8,192 | 5883.03 | 1,392 | — | — |

#### 64 channels — 667,112 parameters (host load 7.3–18.9, mean 10.7)

| device | batch | best ms | samples/sec | peak alloc | % shared pool |
|---|---|---|---|---|---|
| cuda | 256 | 34.78 | **7,360** | 162.10 MiB | 0.17% |
| cuda | 1,024 | 89.79 | **11,405** | 430.98 MiB | 0.44% |
| cuda | 4,096 | 234.72 | **17,450** | 1.47 GiB | 1.55% |
| cuda | 8,192 | 437.47 | **18,726** | 2.87 GiB | 3.03% |
| cpu | 256 | 43.45 | 5,892 | — | — |
| cpu | 1,024 | 203.23 | 5,039 | — | — |
| cpu | 4,096 | 1104.60 | 3,708 | — | — |
| cpu | 8,192 | 2264.58 | 3,617 | — | — |

#### GPU speedup over CPU

| batch | 128 ch | 64 ch |
|---|---|---|
| 256 | 2.05x | **1.25x** |
| 1,024 | 3.02x | 2.26x |
| 4,096 | 4.49x | 4.71x |
| 8,192 | 4.42x | 5.18x |

### 5.3 Thermal and power behaviour

Sampled from amdgpu sysfs at 5 Hz (more reliable than `rocm-smi`, whose concise
table reports SCLK as `N/A` on this APU while `hwmon/freq1_input` has the real
shader clock).

| metric | range observed |
|---|---|
| edge temperature | 59–82 °C |
| socket power | 33–114 W (**whole APU package**, not a GPU rail) |
| shader clock | 600–2899 MHz |
| GPU busy | up to 100% |

The important effect is **CPU/GPU power coupling**, which is specific to an APU:

| host load | mean shader clock |
|---|---|
| 41 | 1,059 MHz |
| 32 | 1,172 MHz |
| 20.6 | 1,577 MHz |
| 10.7 | **2,084 MHz** (peaks 2,899) |

CPU and GPU share one socket power budget. Heavy CPU work directly suppresses
GPU clocks — nearly 2x here. **Do not run a heavy CPU job next to GPU training
on this machine and expect either to hit its numbers.** This also means the
GPU-vs-CPU speedups in 5.2 are, if anything, understated: the GPU was clock-
starved by the very contention that was also slowing the CPU baseline.

---

## 6. A CPU bug found along the way

While validating, `numpy`'s multithreaded float32 GEMM was found to return
**non-deterministic wrong results** on this Zen 5 host — ~0.1-1% of output
elements wrong by O(10), five orders of magnitude beyond fp32 rounding, with a
different answer on every run. It is an OpenBLAS threading race, not bad
hardware (proven by a same-process oneDNN control arm: 40/40 corrupt versus
0/40).

This is written up separately in **`gpu-02-openblas-sgemm-race.md`**, with a
reproducer at `approaches/lifetime-objective/gpu/openblas_sgemm_race.py`.

Two consequences for this document:

1. `activate.sh` exports `OPENBLAS_NUM_THREADS=1`. This costs nothing —
   PyTorch's CPU math goes through oneDNN, which stays threaded across all
   cores and is unaffected.
2. **A correction.** Before the cause was understood, the conv-net checks
   appeared to show the GPU being far more accurate than the CPU (grad error
   ~4e-8 vs ~1e-2). That was this bug, not a real precision difference. With
   `OPENBLAS_NUM_THREADS=1` the two agree to ~1e-7. No claim of intrinsic GPU
   accuracy superiority should be drawn from this work.

---

## 7. Verdict: is the iGPU worth using?

**Yes, but the margin is small at this scale.**

- **For 128-channel / 6-block training: use the GPU.** 2-4.5x, with GPU memory a
  non-issue (5.81% of pool at batch 8192) and the gap widening with batch size.
- **For 64-channel NNUE-scale nets at small batch: the CPU is genuinely
  competitive.** At batch 256 the GPU leads by only **1.25x** (7,360 vs 5,892
  samples/sec). That is *not* worth the ROCm operational burden — the
  `LD_PRELOAD`, the BatchNorm restriction, the driver-version coupling — if a
  workload is dominated by small-batch steps. The CPU path is simpler and, with
  `OPENBLAS_NUM_THREADS=1`, numerically sound.
- **The GPU's advantage is batch size, not raw speed.** GPU throughput rises
  2.5x from batch 256 to 8192 while CPU throughput *falls* ~40% (cache
  pressure). Any training loop intending to use the GPU should be written to
  push large batches.
- **Use bf16.** 11-14x over fp32 on GEMM. The fp32 path on gfx1151 is poorly
  optimised and leaves most of the hardware idle.
- **Self-play rollouts remain CPU/native work.** Only the neural training step
  belongs on the GPU; there is no evidence here that game simulation should move.

A fair summary: the GPU is a solid 3-5x accelerator for large-batch training of
a 0.7M-2M parameter net, and roughly a wash for small-batch work on the smaller
net. Both numbers are lower bounds taken under contention.

---

## 8. Known limitations

1. **All throughput numbers were taken on a contended host** (load 10-41 of 32
   CPUs) because other agents held the machine throughout. Best-of-N bounds but
   does not remove the effect. **Re-measure on an idle host before promoting
   any performance claim.** A brief quiet window suggested the GPU figures are
   ~1.8x pessimistic.
2. **BatchNorm training is unusable** on `.venv-rocm`. GroupNorm is the
   workaround; its effect on learning quality has not been evaluated here.
3. **`LD_PRELOAD` couples the venv to the system ROCm install.** A ROCm upgrade
   or downgrade under `/opt/rocm` could break or fix things independently of the
   wheel. `.venv-rocm-therock` does not have this coupling.
4. **GPU device access depends on a logind seat ACL**, not group membership. A
   headless/SSH/systemd context may lose GPU access entirely.
5. **CPU governor is `powersave`** (EPP `balance_performance`). Not changed —
   it is a system-level setting. Switching to `performance` may raise both CPU
   and GPU numbers and is worth testing.
6. **The existing machine profile is imprecise about this GPU**
   (`research/system-profiles/MACH-20260820T080056Z-376ada90.json` records
   target `gfx11` and `memoryBytes: null`). The accurate values are `gfx1151`
   and a 94.94 GiB *unified* pool. Not edited — that record belongs to another
   work package.
7. **Only one wheel index and two torch versions were compared.** No attempt was
   made to tune rocBLAS/hipBLASLt, to use Triton or `torch.compile`, or to test
   multi-stream or graph capture.
8. bf16 *training* (autocast) throughput was not captured — the run was still
   in progress when the host load returned to ~48 and the figure would have
   been meaningless. `bench.py --amp` will produce it on an idle machine.

---

## 9. How to reproduce

```bash
# one-time
/usr/bin/python3.13 -m venv .venv-rocm
.venv-rocm/bin/python -m pip install --upgrade pip
.venv-rocm/bin/python -m pip install \
    --index-url https://download.pytorch.org/whl/rocm7.1 torch torchvision

# every session — sets LD_PRELOAD, OPENBLAS_NUM_THREADS, alloc conf, MIOpen cache
source approaches/lifetime-objective/gpu/activate.sh

# verify the GPU computes correct results (exits non-zero on real failure)
python approaches/lifetime-objective/gpu/bench.py --correctness

# full suite
python approaches/lifetime-objective/gpu/bench.py --all --json runs/<run-id>/gpu-bench.json

# individual stages
python approaches/lifetime-objective/gpu/bench.py --probe
python approaches/lifetime-objective/gpu/bench.py --matmul --devices cuda,cpu
python approaches/lifetime-objective/gpu/bench.py --train  --devices cuda,cpu \
       --batches 256,1024,4096,8192 --channels 128 --blocks 6
python approaches/lifetime-objective/gpu/bench.py --train --devices cuda --amp   # bf16
```

The venv does **not** have to live in the repository. `activate.sh` resolves it
from `$D7_VENV` first, then a list of out-of-tree candidates, so relocating it
requires no edit:

```bash
export D7_VENV=/path/to/venv-rocm
source approaches/lifetime-objective/gpu/activate.sh
```

### Required environment

| Variable | Value | Why |
|---|---|---|
| `LD_PRELOAD` | `/opt/rocm/lib/libhsa-runtime64.so.1` | **Mandatory** on `.venv-rocm`; without it every `.cuda()` segfaults |
| `OPENBLAS_NUM_THREADS` | `1` | Correctness — see `gpu-02` |
| `PYTORCH_HIP_ALLOC_CONF` | `expandable_segments:True,max_split_size_mb:512` | Unified memory: don't pin a high-water mark the host then can't use |
| `MIOPEN_USER_DB_PATH` | `$D7_VENV/miopen-cache` | Persist tuned kernels; inside the venv, which git already self-ignores |
| `OMP_NUM_THREADS` | `16` | Physical core count |
| `HSA_OVERRIDE_GFX_VERSION` | **unset** | Not needed; gfx1151 is natively supported |

All of these are set by `activate.sh`.
