#!/usr/bin/env python3
"""Standalone reproducer: multithreaded OpenBLAS SGEMM returns wrong results.

Found on an AMD Ryzen AI MAX+ 395 ("Strix Halo", Zen 5 / znver5) while setting
up GPU training for this repository. Written up in
docs/exploratory/gpu-02-openblas-sgemm-race.md.

WHAT IT SHOWS
    numpy's float32 matrix multiply silently returns wrong values for a small
    fraction (~0.1-1%) of output elements once OpenBLAS uses >= 4 threads. The
    wrong values differ from run to run for byte-identical inputs, so the
    failure is a data race, not a precision or rounding effect: the errors are
    O(10), roughly five orders of magnitude larger than fp32 rounding for these
    matrices (~1e-4).

    float64 (DGEMM) is unaffected. 1-2 threads are always correct. The same
    GEMM through a different library (oneDNN, via PyTorch) on the same machine,
    in the same process, under the same load never reproduces it -- which is
    what rules out faulty silicon.

DEPENDENCIES
    numpy only. torch is optional and, when importable, adds the
    second-library control arm that distinguishes "library bug" from "bad CPU".

USAGE
    OPENBLAS_NUM_THREADS=32 python openblas_sgemm_race.py
    python openblas_sgemm_race.py --sweep       # threads x size envelope
"""

from __future__ import annotations

import argparse
import ctypes
import glob
import hashlib
import os
import platform
import sys

import numpy as np

# Absolute error threshold separating "corrupt" from "rounding".
#
# For A, B ~ N(0,1) of inner dimension k, a correct fp32 GEMM has error of order
# eps32 * k  (~1e-4 for k=1024). Observed corruption is O(10..40). A threshold of
# 1.0 sits four orders above the former and well below the latter, so it never
# misclassifies either. Do NOT tighten this to ~1e-2: at k >= 2048 legitimate
# fp32 rounding starts to cross that line and produces false positives.
TOL = 1.0


def openblas_info() -> dict[str, str]:
    """Best-effort identification of the OpenBLAS actually loaded by numpy."""
    info: dict[str, str] = {}
    pats = [
        os.path.join(os.path.dirname(np.__file__), "..", "numpy.libs", "*openblas*"),
        os.path.join(os.path.dirname(np.__file__), ".libs", "*openblas*"),
    ]
    libs = [p for pat in pats for p in glob.glob(pat)]
    if not libs:
        return info
    info["library"] = os.path.basename(libs[0])
    try:
        h = ctypes.CDLL(libs[0])
    except OSError:
        return info
    # scipy-openblas wheels rename and ILP64-suffix their exported symbols.
    for key, bases in (("config", ("openblas_get_config", "scipy_openblas_get_config")),
                       ("core", ("openblas_get_corename", "scipy_openblas_get_corename"))):
        for base in bases:
            for name in (base + "64_", base):
                fn = getattr(h, name, None)
                if fn is not None:
                    fn.restype = ctypes.c_char_p
                    try:
                        info[key] = fn().decode()
                    except Exception:
                        pass
                    break
            if key in info:
                break
    return info


def trial(n: int, reps: int, dtype=np.float32) -> tuple[float, float, int]:
    """Return (mean % corrupt elements, max abs error, distinct results/reps).

    `distinct` needs no reference at all: for fixed inputs and a fixed thread
    count a correct GEMM must be bit-reproducible. distinct > 1 is by itself
    proof of a race.
    """
    rng = np.random.default_rng(0)
    a = rng.standard_normal((n, n)).astype(dtype)
    b = rng.standard_normal((n, n)).astype(dtype)
    ref = a.astype(np.float64) @ b.astype(np.float64)
    hashes: set[str] = set()
    bad_pct: list[float] = []
    worst = 0.0
    for _ in range(reps):
        c = a @ b
        hashes.add(hashlib.md5(np.ascontiguousarray(c).tobytes()).hexdigest())
        d = np.abs(c.astype(np.float64) - ref)
        bad_pct.append(float((d > TOL).mean() * 100.0))
        worst = max(worst, float(d.max()))
    return float(np.mean(bad_pct)), worst, len(hashes)


def control_arm(n: int, reps: int) -> str:
    """Same GEMM via oneDNN (PyTorch) instead of OpenBLAS, if torch is present."""
    try:
        import torch
    except Exception:
        return "  control (oneDNN/torch): torch not installed -- control arm skipped"
    rng = np.random.default_rng(0)
    a = rng.standard_normal((n, n)).astype(np.float32)
    b = rng.standard_normal((n, n)).astype(np.float32)
    ref = a.astype(np.float64) @ b.astype(np.float64)
    at, bt = torch.from_numpy(a).clone(), torch.from_numpy(b).clone()
    reft = torch.from_numpy(ref)
    bad = 0
    worst = 0.0
    for _ in range(reps):
        d = ((at @ bt).double() - reft).abs()
        worst = max(worst, float(d.max()))
        if bool((d > TOL).any()):
            bad += 1
    return (f"  control (oneDNN/torch, {torch.get_num_threads()} threads): "
            f"{bad}/{reps} runs corrupt, max_err={worst:.3e}")


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--n", type=int, default=1024, help="matrix dimension")
    p.add_argument("--reps", type=int, default=10, help="repeats per configuration")
    p.add_argument("--sweep", action="store_true",
                   help="sweep thread counts and sizes")
    args = p.parse_args(argv)

    print(f"python   : {sys.version.split()[0]}")
    print(f"numpy    : {np.__version__}")
    print(f"platform : {platform.platform()}")
    print(f"cpu      : {platform.processor() or 'unknown'} "
          f"({os.cpu_count()} logical)")
    for k, v in openblas_info().items():
        print(f"openblas {k:8s}: {v}")
    print(f"OPENBLAS_NUM_THREADS={os.environ.get('OPENBLAS_NUM_THREADS', 'unset')}  "
          f"OMP_NUM_THREADS={os.environ.get('OMP_NUM_THREADS', 'unset')}")
    print(f"corruption threshold: |err| > {TOL} "
          f"(fp32 rounding for n={args.n} is ~{np.finfo(np.float32).eps * args.n:.1e})")
    print()

    if args.sweep:
        print("NOTE: OPENBLAS_NUM_THREADS is read once at load time, so this "
              "in-process sweep\n      cannot change it. Re-run this script per "
              "thread count, e.g.\n"
              "        for t in 1 2 4 8 16 32; do "
              "OPENBLAS_NUM_THREADS=$t python openblas_sgemm_race.py; done\n")
        for n in (512, 1024, 2048, 4096):
            pct, worst, distinct = trial(n, args.reps)
            verdict = "RACE" if distinct > 1 else ("CORRUPT" if pct else "ok")
            print(f"  n={n:5d}  corrupt={pct:6.3f}%  max_err={worst:.3e}  "
                  f"distinct={distinct}/{args.reps}  {verdict}")
    else:
        pct, worst, distinct = trial(args.n, args.reps)
        print(f"  numpy/OpenBLAS SGEMM n={args.n}: corrupt={pct:.3f}%  "
              f"max_err={worst:.3e}  distinct_results={distinct}/{args.reps}")
        pct64, worst64, distinct64 = trial(args.n, max(3, args.reps // 3), np.float64)
        print(f"  numpy/OpenBLAS DGEMM n={args.n}: "
              f"distinct_results={distinct64}/{max(3, args.reps // 3)} "
              f"(fp64 control)")
        print(control_arm(args.n, args.reps))
        print()
        if distinct > 1 or pct > 0:
            print("  VERDICT: REPRODUCED -- multithreaded SGEMM is returning "
                  "wrong and/or\n           non-reproducible results.")
            return 1
        print("  VERDICT: not reproduced at this thread count / size. "
              "Try more threads.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
