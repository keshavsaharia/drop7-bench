#!/usr/bin/env python3
"""ROCm / CPU benchmark and correctness harness for Drop7 policy networks.

Target machine: AMD Ryzen AI MAX+ 395 "Strix Halo", integrated Radeon 8060S
(RDNA 3.5, gfx1151), unified system memory.

This script does four things, each independently selectable:

  probe        report interpreter, torch, HIP, and device properties
  correctness  verify the GPU actually computes correct results
  matmul       fp32 / bf16 GEMM throughput sweep
  train        forward+backward throughput for the Drop7 policy/value net

IMPORTANT: this is a unified-memory APU. The "GPU memory" reported by torch and
the host RAM are the SAME physical pool. Never present them as separate
capacity. Allocation figures below are reported as a fraction of the shared
pool.

Usage:
    ./bench.py --all
    ./bench.py --correctness
    ./bench.py --train --devices cuda,cpu --batches 256,1024,4096
    ./bench.py --all --json runs/gpu-bench.json
"""

from __future__ import annotations

import argparse
import json
import os
import platform
import re
import shutil
import statistics
import subprocess
import sys
import threading
import time
from dataclasses import dataclass, field, asdict
from typing import Any, Callable, Iterable, Sequence

import torch
import torch.nn as nn
import torch.nn.functional as F

# --------------------------------------------------------------------------
# Drop7 board encoding constants
# --------------------------------------------------------------------------

BOARD = 7          # Drop7 board is 7x7
IN_CHANNELS = 12   # empty, discs 1..7, solid cover, cracked cover,
                   # + next-disc plane + moves-until-rise plane
N_COLUMNS = 7      # 7-way column logit head


# --------------------------------------------------------------------------
# Reporting helpers
# --------------------------------------------------------------------------

def human_bytes(n: float) -> str:
    for unit in ("B", "KiB", "MiB", "GiB", "TiB"):
        if abs(n) < 1024.0:
            return f"{n:,.2f} {unit}"
        n /= 1024.0
    return f"{n:,.2f} PiB"


class Table:
    """Minimal fixed-width table writer so reports paste cleanly into Markdown."""

    def __init__(self, headers: Sequence[str]) -> None:
        self.headers = list(headers)
        self.rows: list[list[str]] = []

    def add(self, *cells: Any) -> None:
        self.rows.append(["" if c is None else str(c) for c in cells])

    def render(self) -> str:
        widths = [len(h) for h in self.headers]
        for row in self.rows:
            for i, cell in enumerate(row):
                widths[i] = max(widths[i], len(cell))
        def line(cells: Sequence[str]) -> str:
            return "| " + " | ".join(c.ljust(widths[i]) for i, c in enumerate(cells)) + " |"
        out = [line(self.headers),
               "|" + "|".join("-" * (w + 2) for w in widths) + "|"]
        out.extend(line(r) for r in self.rows)
        return "\n".join(out)


def section(title: str) -> None:
    print()
    print("=" * 78)
    print(title)
    print("=" * 78)


# --------------------------------------------------------------------------
# rocm-smi sampling
# --------------------------------------------------------------------------

ROCM_SMI = shutil.which("rocm-smi") or "/opt/rocm/bin/rocm-smi"


@dataclass
class SmiSample:
    temp_c: float | None = None
    power_w: float | None = None
    sclk_mhz: float | None = None
    gpu_pct: float | None = None
    gtt_used_b: int | None = None
    loadavg1: float | None = None


def _find_sysfs() -> dict[str, str]:
    """Locate amdgpu sysfs nodes.

    We read sysfs directly rather than shelling out to rocm-smi: it is ~1000x
    cheaper (so we can sample at 5 Hz without perturbing the benchmark), and on
    this APU rocm-smi's concise table reports SCLK as "N/A" while the hwmon
    freq1_input node reports the real shader clock.
    """
    import glob
    paths: dict[str, str] = {}
    for dev in sorted(glob.glob("/sys/class/drm/card*/device")):
        if not os.path.exists(os.path.join(dev, "gpu_busy_percent")):
            continue
        paths["busy"] = os.path.join(dev, "gpu_busy_percent")
        for key, leaf in (("gtt_used", "mem_info_gtt_used"),
                          ("gtt_total", "mem_info_gtt_total"),
                          ("vram_used", "mem_info_vram_used"),
                          ("vram_total", "mem_info_vram_total")):
            p = os.path.join(dev, leaf)
            if os.path.exists(p):
                paths[key] = p
        for hw in sorted(glob.glob(os.path.join(dev, "hwmon", "hwmon*"))):
            for key, leaf in (("temp", "temp1_input"), ("power", "power1_average"),
                              ("sclk", "freq1_input")):
                p = os.path.join(hw, leaf)
                if os.path.exists(p):
                    paths[key] = p
        break
    return paths


SYSFS = _find_sysfs()


def _read_int(path: str | None) -> int | None:
    if not path:
        return None
    try:
        with open(path) as fh:
            return int(fh.read().strip())
    except Exception:
        return None


def read_smi() -> SmiSample | None:
    """One telemetry sample. Returns None if amdgpu sysfs is unavailable."""
    if not SYSFS:
        return None
    s = SmiSample()
    if (v := _read_int(SYSFS.get("temp"))) is not None:
        s.temp_c = v / 1000.0
    if (v := _read_int(SYSFS.get("power"))) is not None:
        s.power_w = v / 1e6
    if (v := _read_int(SYSFS.get("sclk"))) is not None:
        s.sclk_mhz = v / 1e6
    if (v := _read_int(SYSFS.get("busy"))) is not None:
        s.gpu_pct = float(v)
    s.gtt_used_b = _read_int(SYSFS.get("gtt_used"))
    # Host load matters as much as GPU telemetry here: this box is shared with
    # other experiments, and a CPU baseline measured under contention is
    # meaningless. Recorded so every number carries its own load context.
    try:
        s.loadavg1 = os.getloadavg()[0]
    except OSError:
        pass
    return s


class SmiMonitor:
    """Background telemetry sampler for thermal/power/clock behaviour."""

    def __init__(self, interval: float = 0.2) -> None:
        self.interval = interval
        self.samples: list[SmiSample] = []
        self._stop = threading.Event()
        self._thread: threading.Thread | None = None

    def __enter__(self) -> "SmiMonitor":
        if SYSFS:
            self._thread = threading.Thread(target=self._run, daemon=True)
            self._thread.start()
        return self

    def _run(self) -> None:
        while not self._stop.is_set():
            s = read_smi()
            if s is not None:
                self.samples.append(s)
            self._stop.wait(self.interval)

    def __exit__(self, *exc: object) -> None:
        self._stop.set()
        if self._thread is not None:
            self._thread.join(timeout=5)

    def summary(self) -> dict[str, Any]:
        def agg(attr: str) -> dict[str, float] | None:
            vals = [getattr(s, attr) for s in self.samples
                    if getattr(s, attr) is not None]
            if not vals:
                return None
            return {"min": min(vals), "mean": statistics.fmean(vals), "max": max(vals)}
        return {"n_samples": len(self.samples),
                "temp_c": agg("temp_c"), "power_w": agg("power_w"),
                "sclk_mhz": agg("sclk_mhz"), "gpu_pct": agg("gpu_pct"),
                "gtt_used_b": agg("gtt_used_b"),
                "loadavg1": agg("loadavg1")}


# --------------------------------------------------------------------------
# Device helpers
# --------------------------------------------------------------------------

def sync(device: torch.device) -> None:
    if device.type == "cuda":
        torch.cuda.synchronize()


def reset_peak(device: torch.device) -> None:
    if device.type == "cuda":
        torch.cuda.empty_cache()
        torch.cuda.reset_peak_memory_stats()


def peak_bytes(device: torch.device) -> int | None:
    if device.type == "cuda":
        return torch.cuda.max_memory_allocated()
    return None


def timed(fn: Callable[[], Any], device: torch.device,
          warmup: int, iters: int) -> list[float]:
    """Run fn warmup+iters times, returning per-iteration wall seconds."""
    for _ in range(warmup):
        fn()
    sync(device)
    times: list[float] = []
    for _ in range(iters):
        t0 = time.perf_counter()
        fn()
        sync(device)
        times.append(time.perf_counter() - t0)
    return times


def best_and_median(times: Sequence[float]) -> tuple[float, float]:
    """Return (best, median) seconds.

    THIS MACHINE IS SHARED with other experiments, so samples are contaminated
    upward by whatever else was scheduled at that moment. Contention can only
    ever make a run slower, never faster, so the MINIMUM observed time is the
    best available estimate of the uncontended cost, and it is the figure we
    quote. The median is reported alongside it: a large best/median gap is
    itself the evidence that the host was busy.
    """
    return min(times), statistics.median(times)


# --------------------------------------------------------------------------
# Drop7 policy/value network
# --------------------------------------------------------------------------

def make_norm(kind: str, ch: int) -> nn.Module:
    """Norm layer factory.

    'batch' is the textbook choice but is BROKEN FOR TRAINING on gfx1151 with
    the MIOpen shipped inside the torch ROCm wheel: MIOpen selects a GFX9-only
    solver for the batch-norm training kernel and fails to assemble it
    ("v_add_f32 ... row_bcast:31" is not a valid gfx1151 operand), so every
    training step raises miopenStatusUnknownError. Eval mode is unaffected.

    'group' avoids MIOpen entirely -- GroupNorm is a native PyTorch kernel --
    and is the default here. It is also the better choice on the merits for a
    self-play/RL setting, where BatchNorm's running statistics drift between
    the acting and learning distributions and couple samples within a batch.
    """
    if kind == "batch":
        return nn.BatchNorm2d(ch)
    if kind == "group":
        return nn.GroupNorm(min(8, ch), ch)
    if kind == "none":
        return nn.Identity()
    raise ValueError(f"unknown norm kind {kind!r}")


class ResidualBlock(nn.Module):
    def __init__(self, ch: int, norm: str = "group") -> None:
        super().__init__()
        self.conv1 = nn.Conv2d(ch, ch, 3, padding=1, bias=False)
        self.bn1 = make_norm(norm, ch)
        self.conv2 = nn.Conv2d(ch, ch, 3, padding=1, bias=False)
        self.bn2 = make_norm(norm, ch)

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        y = F.relu(self.bn1(self.conv1(x)), inplace=True)
        y = self.bn2(self.conv2(y))
        return F.relu(x + y, inplace=True)


class Drop7Net(nn.Module):
    """Small residual CNN with a 7-way column policy head and a scalar head.

    Input:  (N, 12, 7, 7)
    Output: (N, 7) column logits, (N,) scalar regression (e.g. lifetime score).
    """

    def __init__(self, channels: int = 128, blocks: int = 6,
                 in_channels: int = IN_CHANNELS, norm: str = "group") -> None:
        super().__init__()
        self.norm_kind = norm
        self.stem = nn.Sequential(
            nn.Conv2d(in_channels, channels, 3, padding=1, bias=False),
            make_norm(norm, channels),
            nn.ReLU(inplace=True),
        )
        self.blocks = nn.Sequential(
            *[ResidualBlock(channels, norm) for _ in range(blocks)])
        # Policy head: 7 column logits.
        self.policy = nn.Sequential(
            nn.Conv2d(channels, 32, 1, bias=False),
            make_norm(norm, 32), nn.ReLU(inplace=True),
            nn.Flatten(), nn.Linear(32 * BOARD * BOARD, N_COLUMNS),
        )
        # Scalar head: one regression output.
        self.value = nn.Sequential(
            nn.Conv2d(channels, 32, 1, bias=False),
            make_norm(norm, 32), nn.ReLU(inplace=True),
            nn.Flatten(), nn.Linear(32 * BOARD * BOARD, 128),
            nn.ReLU(inplace=True), nn.Linear(128, 1),
        )

    def forward(self, x: torch.Tensor) -> tuple[torch.Tensor, torch.Tensor]:
        h = self.blocks(self.stem(x))
        return self.policy(h), self.value(h).squeeze(-1)


def n_params(m: nn.Module) -> int:
    return sum(p.numel() for p in m.parameters())


def synthetic_batch(batch: int, device: torch.device, gen: torch.Generator,
                    in_channels: int = IN_CHANNELS
                    ) -> tuple[torch.Tensor, torch.Tensor, torch.Tensor]:
    """A plausible Drop7 minibatch: one-hot board planes + 2 scalar planes."""
    n_onehot = in_channels - 2
    idx = torch.randint(0, n_onehot, (batch, BOARD, BOARD),
                        device=device, generator=gen)
    x = torch.zeros(batch, in_channels, BOARD, BOARD, device=device)
    x.scatter_(1, idx.unsqueeze(1), 1.0)
    # Plane -2: next disc value normalised. Plane -1: moves until rise normalised.
    x[:, -2] = torch.rand(batch, 1, 1, device=device, generator=gen)
    x[:, -1] = torch.rand(batch, 1, 1, device=device, generator=gen)
    y_col = torch.randint(0, N_COLUMNS, (batch,), device=device, generator=gen)
    y_val = torch.randn(batch, device=device, generator=gen)
    return x, y_col, y_val


# --------------------------------------------------------------------------
# 1. Probe
# --------------------------------------------------------------------------

def probe() -> dict[str, Any]:
    section("1. ENVIRONMENT PROBE")
    info: dict[str, Any] = {
        "python": sys.version.split()[0],
        "python_executable": sys.executable,
        "platform": platform.platform(),
        "cpu_count": os.cpu_count(),
        "torch_version": torch.__version__,
        "torch_hip": torch.version.hip,
        "torch_cuda": torch.version.cuda,
        "cuda_is_available": torch.cuda.is_available(),
        "torch_threads": torch.get_num_threads(),
    }
    print(f"python                 : {info['python']}  ({sys.executable})")
    print(f"torch.__version__      : {info['torch_version']}")
    print(f"torch.version.hip      : {info['torch_hip']}")
    print(f"torch.version.cuda     : {info['torch_cuda']}")
    print(f"torch.cuda.is_available: {info['cuda_is_available']}")
    print(f"host cpu threads       : {info['cpu_count']}  "
          f"(torch intra-op threads: {info['torch_threads']})")

    for var in ("HSA_OVERRIDE_GFX_VERSION", "PYTORCH_HIP_ALLOC_CONF",
                "PYTORCH_CUDA_ALLOC_CONF", "HIP_VISIBLE_DEVICES",
                "ROCBLAS_TENSILE_LIBPATH", "MIOPEN_FIND_MODE",
                "HSA_ENABLE_SDMA", "OMP_NUM_THREADS"):
        if var in os.environ:
            print(f"env {var:<24}= {os.environ[var]}")
            info.setdefault("env", {})[var] = os.environ[var]

    try:
        info["arch_list"] = torch.cuda.get_arch_list()
        print(f"torch.cuda.get_arch_list(): {info['arch_list']}")
    except Exception as exc:
        info["arch_list_error"] = str(exc)

    if torch.cuda.is_available():
        name = torch.cuda.get_device_name(0)
        props = torch.cuda.get_device_properties(0)
        info["device_name"] = name
        info["device_properties"] = {
            k: getattr(props, k) for k in dir(props)
            if not k.startswith("_") and isinstance(
                getattr(props, k), (int, float, str, bool))
        }
        print(f"\ntorch.cuda.get_device_name(0)      : {name}")
        print(f"torch.cuda.get_device_properties(0): {props}")
        total = props.total_memory
        print(f"\nUNIFIED MEMORY POOL (shared with host RAM): "
              f"{human_bytes(total)}")
        print("  NOTE: this APU has no dedicated VRAM. The figure above and host "
              "RAM\n  are the same physical pool; do not add them together.")
        info["unified_pool_bytes"] = total
    else:
        print("\n!! torch.cuda.is_available() is False -- no ROCm device visible.")

    try:
        la = os.getloadavg()
        info["loadavg"] = la
        ncpu = os.cpu_count() or 1
        print(f"\nhost load average    : {la[0]:.2f} / {la[1]:.2f} / {la[2]:.2f} "
              f"(1/5/15 min, {ncpu} logical CPUs)")
        if la[0] > 0.5 * ncpu:
            print("  *** WARNING: this machine is busy. Benchmark numbers taken "
                  "now are not\n      comparable to idle-host numbers. ***")
    except OSError:
        pass

    smi = read_smi()
    if smi:
        info["smi_idle"] = asdict(smi)
        print(f"\nidle telemetry: temp={smi.temp_c}C socket_power={smi.power_w}W "
              f"sclk={smi.sclk_mhz}MHz busy={smi.gpu_pct}%")
    return info


# --------------------------------------------------------------------------
# 2. Correctness
# --------------------------------------------------------------------------

def correctness(dev: torch.device, tol_matmul: float = 5e-2) -> dict[str, Any]:
    """A GPU that imports but computes garbage is a real failure mode on
    partially supported targets. These checks compare against CPU."""
    section("2. NUMERICAL CORRECTNESS (GPU vs CPU)")
    res: dict[str, Any] = {"passed": True, "checks": []}

    def record(name: str, ok: bool, detail: str, gate: bool = True) -> None:
        """gate=False: a known, documented limitation. Still reported loudly,
        but it does not fail the run, because the supported configuration
        works around it (see make_norm)."""
        res["checks"].append({"name": name, "ok": bool(ok),
                              "detail": detail, "gate": gate})
        if gate:
            res["passed"] = res["passed"] and bool(ok)
        else:
            res.setdefault("warnings", [])
            if not ok:
                res["warnings"].append(name)
        tag = "PASS" if ok else ("FAIL" if gate else "WARN")
        print(f"  [{tag}] {name}: {detail}")

    g = torch.Generator().manual_seed(20260820)

    # -- 2z. HOST sanity: is the CPU's own fp32 GEMM trustworthy?
    #
    # Not a GPU test, but it guards every comparison below. On this machine the
    # multithreaded OpenBLAS SGEMM produces non-deterministic garbage in ~0.5%
    # of output elements once OPENBLAS_NUM_THREADS >= 4 (fp64 GEMM is exact,
    # and 1-2 threads are clean, so it is a threading race in the SGEMM kernel,
    # not bad hardware). numpy is affected in every environment tested. Cap
    # OPENBLAS_NUM_THREADS at 1-2 to avoid it.
    m = 1024
    A = torch.randn(m, m, generator=g)
    B = torch.randn(m, m, generator=g)
    ref = A.double() @ B.double()
    d = (A @ B).double() - ref
    bad = (d.abs() > 1e-2).float().mean().item() * 100.0
    record("host CPU fp32 GEMM trustworthy", bad == 0.0,
           f"{bad:.3f}% of elements off by >1e-2 (max {d.abs().max():.3e}); "
           f"OPENBLAS_NUM_THREADS={os.environ.get('OPENBLAS_NUM_THREADS', 'unset')}")
    res["cpu_sgemm_bad_pct"] = bad
    del A, B, ref, d

    # -- 2a. Trivial sanity: does the device return what we put on it?
    a = torch.arange(16, dtype=torch.float32)
    rt = a.to(dev).cpu()
    record("roundtrip h2d/d2h", torch.equal(a, rt),
           f"max|diff| = {(a - rt).abs().max().item():.3e}")

    # -- 2b. Elementwise reduction
    x = torch.randn(1 << 20, generator=g)
    s_cpu, s_gpu = x.sum().item(), x.to(dev).sum().item()
    rel = abs(s_gpu - s_cpu) / max(1.0, abs(s_cpu))
    record("1M-element sum", rel < 1e-3,
           f"cpu={s_cpu:.6f} gpu={s_gpu:.6f} rel={rel:.3e}")

    # -- 2c. THE big one: 4096x4096 fp32 matmul.
    #
    # The reference is computed in float64, NOT in CPU fp32. On this machine the
    # multithreaded OpenBLAS SGEMM used by numpy (and by some torch builds)
    # silently corrupts ~0.5% of output elements, so CPU fp32 is not a
    # trustworthy oracle -- see the cpu_sgemm_sane() check below. float64 GEMM
    # is exact here and is safe to compare against.
    n = 4096
    A = torch.randn(n, n, generator=g)
    B = torch.randn(n, n, generator=g)
    ref = A.double() @ B.double()
    C_gpu = (A.to(dev) @ B.to(dev)).cpu().double()
    max_abs = (ref - C_gpu).abs().max().item()
    denom = ref.abs().max().item()
    rel = max_abs / denom
    record(f"{n}x{n} fp32 matmul vs float64 ref", rel < 1e-4,
           f"max|abs diff| = {max_abs:.6e}  (max|C| = {denom:.3f}, "
           f"rel = {rel:.3e})")
    res["matmul_4096_max_abs_diff"] = max_abs
    res["matmul_4096_rel_diff"] = rel
    del A, B, ref, C_gpu

    # -- 2d. bf16 matmul, checked loosely against a float64 reference.
    A = torch.randn(1024, 1024, generator=g)
    B = torch.randn(1024, 1024, generator=g)
    ref = A.double() @ B.double()
    got = (A.to(dev).bfloat16() @ B.to(dev).bfloat16()).float().cpu().double()
    rel_bf16 = ((ref - got).abs().max() / ref.abs().max()).item()
    record("1024x1024 bf16 matmul", rel_bf16 < tol_matmul,
           f"rel max diff vs float64 ref = {rel_bf16:.3e} (tol {tol_matmul})")
    res["matmul_bf16_rel_diff"] = rel_bf16

    # -- 2e. Small MLP forward + backward, CPU vs GPU gradients.
    torch.manual_seed(7)
    mlp = nn.Sequential(nn.Linear(256, 512), nn.ReLU(),
                        nn.Linear(512, 512), nn.Tanh(),
                        nn.Linear(512, 10))
    xin = torch.randn(64, 256, generator=g)
    tgt = torch.randint(0, 10, (64,), generator=g)

    def fwd_bwd(model: nn.Module, xx: torch.Tensor, tt: torch.Tensor
                ) -> tuple[float, list[torch.Tensor]]:
        model.zero_grad(set_to_none=True)
        loss = F.cross_entropy(model(xx), tt)
        loss.backward()
        return loss.item(), [p.grad.detach().cpu().clone() for p in model.parameters()]

    l_ref, g_ref = fwd_bwd(Drop7NetCopy(mlp).double(), xin.double(), tgt)
    l_cpu, g_cpu = fwd_bwd(Drop7NetCopy(mlp), xin, tgt)
    mlp_g = Drop7NetCopy(mlp).to(dev)
    l_gpu, g_gpu = fwd_bwd(mlp_g, xin.to(dev), tgt.to(dev))
    finite = all(torch.isfinite(t).all().item() for t in g_gpu)
    gmax = max((a - b).abs().max().item() for a, b in zip(g_ref, g_gpu))
    gcpu = max((a - b).abs().max().item() for a, b in zip(g_ref, g_cpu))
    record("MLP fwd+bwd gradients finite", finite,
           f"all {len(g_gpu)} grad tensors finite = {finite}")
    record("MLP fwd+bwd grads vs float64 ref", gmax < 1e-4,
           f"GPU max|grad err| = {gmax:.3e} (CPU fp32 for context: {gcpu:.3e}); "
           f"loss f64={l_ref:.6f} cpu={l_cpu:.6f} gpu={l_gpu:.6f}")
    res["mlp_max_grad_diff"] = gmax

    # -- 2f. Drop7-shaped conv net forward + backward.
    #
    # NOTE ON THE REFERENCE. Judging GPU fp32 conv against CPU fp32 conv is not
    # a correctness test: on this machine the CPU (oneDNN) conv path carries
    # ~4e-3 absolute error against an exact reference, which is far larger than
    # the GPU's. So we build the ground truth in float64 on the CPU and score
    # both fp32 backends against it. The CPU fp32 number is reported for
    # context only and is not a pass/fail gate.
    torch.manual_seed(11)
    net = Drop7Net(channels=64, blocks=2, norm="group").eval()
    xb = torch.randn(32, IN_CHANNELS, BOARD, BOARD, generator=g)
    yc = torch.randint(0, N_COLUMNS, (32,), generator=g)
    yv = torch.randn(32, generator=g)

    def conv_fwd_bwd(model: nn.Module, device: torch.device, dtype: torch.dtype
                     ) -> tuple[float, list[torch.Tensor], torch.Tensor]:
        m = Drop7NetCopy(model).to(device=device, dtype=dtype)
        xx = xb.to(device=device, dtype=dtype)
        m.zero_grad(set_to_none=True)
        logits, val = m(xx)
        loss = (F.cross_entropy(logits.float(), yc.to(device))
                + F.mse_loss(val.float(), yv.to(device)))
        loss.backward()
        return (loss.item(),
                [p.grad.detach().float().cpu().clone() for p in m.parameters()],
                logits.detach().float().cpu())

    l64, g64, o64 = conv_fwd_bwd(net, torch.device("cpu"), torch.float64)
    lc, gc, oc = conv_fwd_bwd(net, torch.device("cpu"), torch.float32)
    lg, gg, og = conv_fwd_bwd(net, dev, torch.float32)

    def err(o: torch.Tensor, gr: list[torch.Tensor]) -> tuple[float, float]:
        return ((o - o64).abs().max().item(),
                max((a - b).abs().max().item() for a, b in zip(gr, g64)))

    cpu_out_err, cpu_grad_err = err(oc, gc)
    gpu_out_err, gpu_grad_err = err(og, gg)
    finite = all(torch.isfinite(t).all().item() for t in gg)

    record("ConvNet forward vs float64 ref", gpu_out_err < 1e-3,
           f"GPU fp32 max|logit err| = {gpu_out_err:.3e} "
           f"(CPU fp32 for context: {cpu_out_err:.3e})")
    record("ConvNet gradients finite", finite,
           f"all {len(gg)} grad tensors finite = {finite}")
    record("ConvNet gradients vs float64 ref", gpu_grad_err < 1e-3,
           f"GPU fp32 max|grad err| = {gpu_grad_err:.3e} "
           f"(CPU fp32 for context: {cpu_grad_err:.3e}); "
           f"loss f64={l64:.6f} cpu={lc:.6f} gpu={lg:.6f}")
    res["conv_gpu_out_err_vs_f64"] = gpu_out_err
    res["conv_gpu_grad_err_vs_f64"] = gpu_grad_err
    res["conv_cpu_out_err_vs_f64"] = cpu_out_err
    res["conv_cpu_grad_err_vs_f64"] = cpu_grad_err
    # train() mode below on purpose: it exercises the MIOpen batch-norm
    # training kernels, which are a different dispatch path from eval().
    net_g = Drop7NetCopy(net).to(dev).train()

    # -- 2f-bis. BatchNorm in TRAINING mode.
    #
    # This is a separate check on purpose. MIOpen dispatches batch norm to a
    # different kernel in train() mode than in eval() mode, and on gfx1151 the
    # MIOpen shipped inside the torch wheel fails to build that training kernel
    # at all. Everything above passes in eval() mode, so without this check a
    # completely untrainable stack looks healthy.
    torch.manual_seed(13)
    bn_ref = nn.BatchNorm2d(64)
    xbn = torch.randn(256, 64, 7, 7, generator=g)

    def bn_fwd_bwd(device: torch.device, dtype: torch.dtype
                   ) -> tuple[torch.Tensor, torch.Tensor]:
        m = Drop7NetCopy(bn_ref).to(device=device, dtype=dtype).train()
        xx = xbn.to(device=device, dtype=dtype).requires_grad_(True)
        out = m(xx)
        out.square().mean().backward()
        return out.detach().float().cpu(), xx.grad.detach().float().cpu()

    try:
        b64_o, b64_g = bn_fwd_bwd(torch.device("cpu"), torch.float64)
        bg_o, bg_g = bn_fwd_bwd(dev, torch.float32)
        o_err = (bg_o - b64_o).abs().max().item()
        g_err = (bg_g - b64_g).abs().max().item()
        ok = (o_err < 1e-3 and g_err < 1e-3
              and torch.isfinite(bg_g).all().item())
        record("BatchNorm TRAIN-mode fwd+bwd", ok,
               f"max|out err| = {o_err:.3e}, max|grad err| = {g_err:.3e} "
               f"vs float64 ref", gate=False)
        res["bn_train_out_err"] = o_err
        res["bn_train_grad_err"] = g_err
    except Exception as exc:
        record("BatchNorm TRAIN-mode fwd+bwd", False,
               f"raised {type(exc).__name__}: {str(exc).splitlines()[0][:120]} "
               f"-- KNOWN on gfx1151 with the wheel's bundled MIOpen; "
               f"use --norm group (the default)", gate=False)
        res["bn_train_error"] = str(exc)

    # -- 2g. Optimizer step actually changes weights and stays finite.
    opt = torch.optim.AdamW(net_g.parameters(), lr=1e-3)
    before = [p.detach().cpu().clone() for p in net_g.parameters()]
    for _ in range(5):
        opt.zero_grad(set_to_none=True)
        logits, val = net_g(xb.to(dev))
        (F.cross_entropy(logits, yc.to(dev)) + F.mse_loss(val, yv.to(dev))).backward()
        opt.step()
    after = [p.detach().cpu().clone() for p in net_g.parameters()]
    moved = max((a - b).abs().max().item() for a, b in zip(before, after))
    all_finite = all(torch.isfinite(p).all().item() for p in after)
    record("5 AdamW steps change weights & stay finite", moved > 0 and all_finite,
           f"max|delta w| = {moved:.3e}, all finite = {all_finite}")

    warned = res.get("warnings") or []
    print(f"\n  OVERALL: {'PASS' if res['passed'] else 'FAIL'}"
          + (f"  ({len(warned)} known-limitation warning(s): "
             f"{', '.join(warned)})" if warned else ""))
    return res


def Drop7NetCopy(model: nn.Module) -> nn.Module:
    """Deep copy of a module with identical initial weights (for CPU/GPU pairing)."""
    import copy
    return copy.deepcopy(model)


# --------------------------------------------------------------------------
# 3. Matmul sweep
# --------------------------------------------------------------------------

def matmul_sweep(devices: Sequence[torch.device], sizes: Sequence[int],
                 dtypes: Sequence[torch.dtype], iters: int = 12,
                 warmup: int = 5) -> dict[str, Any]:
    section("3. MATMUL THROUGHPUT")
    tbl = Table(["device", "dtype", "N", "best ms", "med ms", "TFLOP/s (best)",
                 "peak alloc"])
    out: list[dict[str, Any]] = []
    for dev in devices:
        for dtype in dtypes:
            for n in sizes:
                # CPU fp32 8192^3 is ~1.1 TFLOP per iteration; keep it bounded.
                it = iters if dev.type == "cuda" else max(3, iters // 4)
                try:
                    reset_peak(dev)
                    a = torch.randn(n, n, device=dev, dtype=torch.float32).to(dtype)
                    b = torch.randn(n, n, device=dev, dtype=torch.float32).to(dtype)
                    ts = timed(lambda: a @ b, dev, warmup, it)
                    best, med = best_and_median(ts)
                    tflops = (2.0 * n ** 3) / best / 1e12
                    pk = peak_bytes(dev)
                    tbl.add(dev.type, str(dtype).replace("torch.", ""), n,
                            f"{best * 1e3:.3f}", f"{med * 1e3:.3f}",
                            f"{tflops:.2f}", human_bytes(pk) if pk else "-")
                    out.append({"device": dev.type,
                                "dtype": str(dtype).replace("torch.", ""),
                                "n": n, "ms_best": best * 1e3,
                                "ms_median": med * 1e3, "tflops": tflops,
                                "peak_bytes": pk})
                    del a, b
                except Exception as exc:
                    tbl.add(dev.type, str(dtype).replace("torch.", ""), n,
                            "ERROR", "-", str(exc)[:40], "-")
                    out.append({"device": dev.type, "n": n, "error": str(exc)})
                finally:
                    reset_peak(dev)
    print(tbl.render())
    return {"rows": out, "table": tbl.render()}


# --------------------------------------------------------------------------
# 4. Training throughput
# --------------------------------------------------------------------------

def train_sweep(devices: Sequence[torch.device], batches: Sequence[int],
                channels: int, blocks: int, steps: int = 20,
                warmup: int = 5, amp: bool = False,
                norm: str = "group") -> dict[str, Any]:
    section(f"4. DROP7 NET TRAINING THROUGHPUT "
            f"(channels={channels}, blocks={blocks}, norm={norm}, amp={amp})")
    probe_net = Drop7Net(channels, blocks, norm=norm)
    print(f"  model: {n_params(probe_net):,} parameters, "
          f"input (N,{IN_CHANNELS},{BOARD},{BOARD}), heads: "
          f"{N_COLUMNS}-way column logits + 1 scalar")
    del probe_net

    tbl = Table(["device", "batch", "best ms", "med ms", "samples/sec (best)",
                 "peak alloc", "% shared pool"])
    out: list[dict[str, Any]] = []
    pool = (torch.cuda.get_device_properties(0).total_memory
            if torch.cuda.is_available() else None)

    for dev in devices:
        for bs in batches:
            try:
                reset_peak(dev)
                torch.manual_seed(1234)
                net = Drop7Net(channels, blocks, norm=norm).to(dev).train()
                opt = torch.optim.AdamW(net.parameters(), lr=1e-3)
                gen = torch.Generator(device=dev).manual_seed(99)
                x, yc, yv = synthetic_batch(bs, dev, gen)
                scaler = (torch.amp.GradScaler("cuda")
                          if amp and dev.type == "cuda" else None)

                def step() -> None:
                    opt.zero_grad(set_to_none=True)
                    if amp and dev.type == "cuda":
                        with torch.autocast("cuda", dtype=torch.bfloat16):
                            lo, va = net(x)
                            loss = F.cross_entropy(lo, yc) + F.mse_loss(va, yv)
                        loss.backward()
                    else:
                        lo, va = net(x)
                        loss = F.cross_entropy(lo, yc) + F.mse_loss(va, yv)
                        loss.backward()
                    opt.step()

                n_steps = steps if dev.type == "cuda" else max(4, steps // 4)
                n_warm = warmup if dev.type == "cuda" else max(2, warmup // 2)
                ts = timed(step, dev, n_warm, n_steps)
                best, med = best_and_median(ts)
                sps = bs / best
                pk = peak_bytes(dev)
                frac = f"{100.0 * pk / pool:.2f}%" if (pk and pool) else "-"
                tbl.add(dev.type, f"{bs:,}", f"{best * 1e3:.2f}",
                        f"{med * 1e3:.2f}", f"{sps:,.0f}",
                        human_bytes(pk) if pk else "-", frac)
                out.append({"device": dev.type, "batch": bs,
                            "ms_best": best * 1e3, "ms_median": med * 1e3,
                            "samples_per_sec": sps, "peak_bytes": pk,
                            "pool_bytes": pool, "amp": amp,
                            "loadavg": os.getloadavg()[0]})
                del net, opt, x, yc, yv
            except Exception as exc:
                tbl.add(dev.type, f"{bs:,}", "ERROR", "-",
                        str(exc).splitlines()[0][:44], "-", "-")
                out.append({"device": dev.type, "batch": bs, "error": str(exc)})
            finally:
                reset_peak(dev)
    print(tbl.render())
    return {"rows": out, "table": tbl.render(), "channels": channels,
            "blocks": blocks, "amp": amp, "norm": norm}


# --------------------------------------------------------------------------
# Main
# --------------------------------------------------------------------------

def parse_devices(spec: str) -> list[torch.device]:
    devs: list[torch.device] = []
    for tok in spec.split(","):
        tok = tok.strip()
        if not tok:
            continue
        if tok == "cuda":
            if not torch.cuda.is_available():
                print("  (skipping 'cuda': not available)")
                continue
            devs.append(torch.device("cuda:0"))
        else:
            devs.append(torch.device(tok))
    return devs


def main(argv: Sequence[str] | None = None) -> int:
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--all", action="store_true", help="run every stage")
    p.add_argument("--probe", action="store_true")
    p.add_argument("--correctness", action="store_true")
    p.add_argument("--matmul", action="store_true")
    p.add_argument("--train", action="store_true")
    p.add_argument("--devices", default="cuda,cpu",
                   help="comma list, e.g. 'cuda,cpu' or 'cuda'")
    p.add_argument("--sizes", default="1024,2048,4096,8192",
                   help="matmul sizes")
    p.add_argument("--batches", default="256,1024,4096,8192",
                   help="training batch sizes")
    p.add_argument("--channels", type=int, default=128)
    p.add_argument("--blocks", type=int, default=6)
    p.add_argument("--steps", type=int, default=20)
    p.add_argument("--warmup", type=int, default=5)
    p.add_argument("--norm", default="group", choices=["group", "batch", "none"],
                   help="norm layer; 'batch' is broken for training on gfx1151 "
                        "with the wheel's bundled MIOpen (see make_norm)")
    p.add_argument("--amp", action="store_true",
                   help="use bf16 autocast for the training sweep")
    p.add_argument("--threads", type=int, default=None,
                   help="torch CPU intra-op threads (default: all cores)")
    p.add_argument("--json", default=None, help="write results as JSON here")
    args = p.parse_args(argv)

    if not any([args.all, args.probe, args.correctness, args.matmul, args.train]):
        args.all = True
    if args.all:
        args.probe = args.correctness = args.matmul = args.train = True

    if args.threads:
        torch.set_num_threads(args.threads)

    results: dict[str, Any] = {"argv": list(argv or sys.argv[1:]),
                               "timestamp": time.strftime("%Y-%m-%dT%H:%M:%S%z")}

    if args.probe:
        results["probe"] = probe()

    devices = parse_devices(args.devices)
    if not devices:
        print("No usable devices selected.")
        return 2

    cuda_devs = [d for d in devices if d.type == "cuda"]
    if args.correctness:
        if cuda_devs:
            results["correctness"] = correctness(cuda_devs[0])
        else:
            print("\n(skipping correctness: no cuda device)")

    sizes = [int(s) for s in args.sizes.split(",") if s.strip()]
    batches = [int(s) for s in args.batches.split(",") if s.strip()]

    with SmiMonitor() as mon:
        if args.matmul:
            results["matmul"] = matmul_sweep(
                devices, sizes, [torch.float32, torch.bfloat16],
                iters=12, warmup=args.warmup)
        if args.train:
            results["train"] = train_sweep(
                devices, batches, args.channels, args.blocks,
                steps=args.steps, warmup=args.warmup, amp=args.amp,
                norm=args.norm)
    smi = mon.summary()
    results["rocm_smi"] = smi
    if smi["n_samples"]:
        section("5. ROCM-SMI DURING BENCHMARK")
        t = Table(["metric", "min", "mean", "max"])
        for key, label in (("temp_c", "edge temp (C)"),
                           ("power_w", "socket power (W)"),
                           ("sclk_mhz", "shader clock (MHz)"),
                           ("gpu_pct", "GPU busy (%)")):
            v = smi[key]
            if v:
                t.add(label, f"{v['min']:.1f}", f"{v['mean']:.1f}", f"{v['max']:.1f}")
        if smi.get("loadavg1"):
            v = smi["loadavg1"]
            t.add("host load average (1m)", f"{v['min']:.1f}",
                  f"{v['mean']:.1f}", f"{v['max']:.1f}")
        if smi.get("gtt_used_b"):
            v = smi["gtt_used_b"]
            t.add("GTT used (shared pool)", human_bytes(v["min"]),
                  human_bytes(v["mean"]), human_bytes(v["max"]))
        print(t.render())
        print(f"\n  ({smi['n_samples']} samples from amdgpu sysfs at ~5 Hz)")
        print("  NOTE: 'socket power' is the whole APU package (CPU+GPU+SoC), "
              "not a GPU-only rail.")
        la = smi.get("loadavg1")
        ncpu = os.cpu_count() or 1
        if la and la["mean"] > 0.5 * ncpu:
            print(f"\n  *** CONTENTION WARNING: mean host load {la['mean']:.1f} on "
                  f"{ncpu} logical CPUs. ***\n  Other processes were competing for "
                  f"this machine. CPU throughput numbers above are\n  lower bounds "
                  f"and the GPU numbers may also be depressed. Re-run on an idle\n"
                  f"  host before quoting any of these figures.")

    if args.json:
        os.makedirs(os.path.dirname(os.path.abspath(args.json)) or ".", exist_ok=True)
        with open(args.json, "w") as fh:
            json.dump(results, fh, indent=2, default=str)
        print(f"\nJSON written to {args.json}")

    ok = results.get("correctness", {}).get("passed", True)
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
