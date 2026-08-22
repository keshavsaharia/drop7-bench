#!/usr/bin/env python3
"""CMA-ES over the eighteen fair-leaf weights, fitness = mean whole-game score.

Design points that matter scientifically:

* Every generation plays a FRESH, contiguous block of training seeds taken in
  order from one lease.  No seed is ever replayed inside the optimisation, so
  the optimiser cannot specialise to a fixed set of games; it can only move
  toward weights that are better on average across the lease.
* All individuals of a generation play the SAME block (common random numbers),
  and the frozen vector is played on every block as a control.  Ranks inside a
  generation are paired comparisons; the control gives a paired trajectory of
  "population mean minus frozen" that is reported, never used for selection.
* Coordinates are the weight divided by the absolute frozen value, so the
  start is the vector of signs (+1/-1), one sigma is a relative change, and a
  sign flip is reachable.  Nothing clips a coordinate.
* The candidate the experiment freezes is the distribution MEAN at the last
  generation, not the best sampled individual: the best-of-lambda estimate is
  biased upward by selection on its own noisy fitness (winner's curse).
* The run is resumable: a generation whose artifact exists is reloaded, and the
  optimiser state is checkpointed after every update.  A STOP file in the run
  directory ends the run after the current generation.

This file reads no seed itself; all gameplay happens in the evaluator binary.
"""
from __future__ import annotations

import argparse
import json
import math
import os
import subprocess
import sys
import time
from pathlib import Path

import numpy as np

NAMES = [
    "open_columns", "height_load", "solid_cells", "cracked_cells",
    "numbered_cells", "high_low_numbers", "direct_potential",
    "latent_chain_potential", "cracked_exposure", "solid_exposure",
    "adjacent_ones", "triple_twos", "dead_low_numbers", "covered_height_risk",
    "low_number_height_risk", "danger_height_squared", "rise_pressure",
    "next_disc_vertical_options",
]
FROZEN = np.array([
    180.0, -20.0, -620.0, -220.0, -18.0, -90.0, 1600.0, 700.0, 100.0, 40.0,
    -550.0, -750.0, -120.0, -95.0, -85.0, -1250.0, -35.0, 220.0,
])
SCALE = np.abs(FROZEN)
N = len(NAMES)


def decode(x: np.ndarray) -> np.ndarray:
    return x * SCALE


def encode(w: np.ndarray) -> np.ndarray:
    return w / SCALE


def write_weights(path: Path, w: np.ndarray, header: str) -> None:
    with path.open("w") as f:
        f.write(f"# {header}\n")
        for name, value in zip(NAMES, w):
            f.write(f"{name} {value!r}\n")


class CMA:
    """Hansen's (mu/mu_w, lambda)-CMA-ES with mirrored sampling."""

    def __init__(self, x0: np.ndarray, sigma: float, lam: int, seed: int):
        self.n = len(x0)
        self.m = x0.astype(float).copy()
        self.sigma = float(sigma)
        self.lam = lam
        self.mu = lam // 2
        w = np.log(self.mu + 0.5) - np.log(np.arange(1, self.mu + 1))
        self.w = w / w.sum()
        self.mueff = 1.0 / float(np.sum(self.w ** 2))
        n = self.n
        self.cc = (4 + self.mueff / n) / (n + 4 + 2 * self.mueff / n)
        self.cs = (self.mueff + 2) / (n + self.mueff + 5)
        self.c1 = 2 / ((n + 1.3) ** 2 + self.mueff)
        self.cmu = min(1 - self.c1, 2 * (self.mueff - 2 + 1 / self.mueff) / ((n + 2) ** 2 + self.mueff))
        self.damps = 1 + 2 * max(0.0, math.sqrt((self.mueff - 1) / (n + 1)) - 1) + self.cs
        self.chi_n = math.sqrt(n) * (1 - 1 / (4 * n) + 1 / (21 * n * n))
        self.C = np.eye(n)
        self.ps = np.zeros(n)
        self.pc = np.zeros(n)
        self.gen = 0
        self.rng = np.random.default_rng(seed)

    # ---- persistence -----------------------------------------------------
    def to_json(self) -> dict:
        return {
            "m": self.m.tolist(), "sigma": self.sigma, "C": self.C.tolist(),
            "ps": self.ps.tolist(), "pc": self.pc.tolist(), "gen": self.gen,
            "lam": self.lam, "rng": self.rng.bit_generator.state,
        }

    @classmethod
    def from_json(cls, d: dict) -> "CMA":
        c = cls(np.array(d["m"]), d["sigma"], d["lam"], 0)
        c.C = np.array(d["C"]); c.ps = np.array(d["ps"]); c.pc = np.array(d["pc"])
        c.gen = d["gen"]; c.rng.bit_generator.state = d["rng"]
        return c

    # ---- one generation --------------------------------------------------
    def ask(self) -> tuple[np.ndarray, np.ndarray]:
        eigval, B = np.linalg.eigh(self.C)
        eigval = np.maximum(eigval, 1e-20)
        D = np.sqrt(eigval)
        half = self.lam // 2
        z = self.rng.standard_normal((half, self.n))
        z = np.concatenate([z, -z], axis=0)  # mirrored pairs
        y = (B * D) @ z.T  # n x lam
        y = y.T
        x = self.m + self.sigma * y
        self._B, self._D = B, D
        return x, y

    def tell(self, x: np.ndarray, y: np.ndarray, fitness: np.ndarray) -> None:
        """fitness: higher is better."""
        order = np.argsort(-fitness)
        sel_y = y[order[: self.mu]]
        y_w = self.w @ sel_y
        self.m = self.m + self.sigma * y_w
        B, D = self._B, self._D
        C_inv_sqrt = B @ np.diag(1.0 / D) @ B.T
        self.ps = (1 - self.cs) * self.ps + math.sqrt(self.cs * (2 - self.cs) * self.mueff) * (C_inv_sqrt @ y_w)
        self.gen += 1
        hsig = float(np.linalg.norm(self.ps) / math.sqrt(1 - (1 - self.cs) ** (2 * self.gen)) / self.chi_n
                     < 1.4 + 2 / (self.n + 1))
        self.pc = (1 - self.cc) * self.pc + hsig * math.sqrt(self.cc * (2 - self.cc) * self.mueff) * y_w
        rank_mu = sum(wi * np.outer(yi, yi) for wi, yi in zip(self.w, sel_y))
        self.C = ((1 - self.c1 - self.cmu) * self.C
                  + self.c1 * (np.outer(self.pc, self.pc) + (1 - hsig) * self.cc * (2 - self.cc) * self.C)
                  + self.cmu * rank_mu)
        self.C = (self.C + self.C.T) / 2
        self.sigma *= math.exp((self.cs / self.damps) * (np.linalg.norm(self.ps) / self.chi_n - 1))


def run_generation(args, run: Path, gen: int, x: np.ndarray, seed_start: int) -> dict:
    gdir = run / f"gen-{gen:03d}"
    gdir.mkdir(exist_ok=True)
    artifact = gdir / "population.json"
    if not artifact.exists():
        pop = gdir / "population.txt"
        with pop.open("w") as f:
            f.write("# name then 18 weights; 'control' is the frozen vector\n")
            f.write("control " + " ".join(repr(float(v)) for v in FROZEN) + "\n")
            for k, xi in enumerate(x):
                f.write(f"g{gen:03d}-i{k:02d} " + " ".join(repr(float(v)) for v in decode(xi)) + "\n")
        cmd = [args.evaluator, "--population", str(pop), "--output", str(artifact),
               "--label", f"gen-{gen:03d}", "--seed-start", f"0x{seed_start:08x}",
               "--games", str(args.games), "--threads", str(args.threads),
               "--depth", str(args.depth), "--chance-samples", str(args.strata),
               "--cache", str(args.cache), "--max-moves", str(args.max_moves), "--quiet"]
        with (gdir / "evaluate.log").open("w") as log:
            log.write(" ".join(cmd) + "\n")
            log.flush()
            rc = subprocess.call(cmd, stdout=log, stderr=subprocess.STDOUT)
        if rc != 0:
            raise SystemExit(f"evaluator failed (rc={rc}) in generation {gen}; see {gdir}/evaluate.log")
    return json.loads(artifact.read_text())


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--run-dir", required=True)
    p.add_argument("--evaluator", default="build/leaf-evolution/evaluate")
    p.add_argument("--lease-start", required=True, help="first seed of the training lease (hex)")
    p.add_argument("--lease-end", required=True, help="exclusive end of the training lease (hex)")
    p.add_argument("--games", type=int, default=32, help="games per individual per generation")
    p.add_argument("--lambda", dest="lam", type=int, default=16)
    p.add_argument("--sigma0", type=float, default=0.25)
    p.add_argument("--depth", type=int, default=4)
    p.add_argument("--strata", type=int, default=5)
    p.add_argument("--cache", type=int, default=60000)
    p.add_argument("--max-moves", type=int, default=2000)
    p.add_argument("--threads", type=int, default=30)
    p.add_argument("--max-generations", type=int, default=40)
    p.add_argument("--wall-hours", type=float, default=14.0)
    p.add_argument("--sigma-floor", type=float, default=0.02)
    p.add_argument("--cma-seed", type=lambda s: int(s, 0), default=0xC3A5EED5)
    args = p.parse_args()

    run = Path(args.run_dir)
    run.mkdir(parents=True, exist_ok=True)
    lease_start = int(args.lease_start, 0)
    lease_end = int(args.lease_end, 0)
    if args.lam % 2 != 0:
        raise SystemExit("--lambda must be even (mirrored sampling)")

    state_path = run / "cma-state.json"
    if state_path.exists():
        cma = CMA.from_json(json.loads(state_path.read_text()))
        print(f"resumed at generation {cma.gen}, sigma {cma.sigma:.4f}", file=sys.stderr)
    else:
        cma = CMA(encode(FROZEN), args.sigma0, args.lam, args.cma_seed)
        (run / "config.json").write_text(json.dumps({
            "format": "drop7-leaf-evolution-config-v1",
            "dims": N, "names": NAMES, "frozen": FROZEN.tolist(), "scale": SCALE.tolist(),
            "lambda": args.lam, "mu": cma.mu, "sigma0": args.sigma0, "mirrored": True,
            "gamesPerIndividual": args.games, "depth": args.depth, "chanceSamples": args.strata,
            "cache": args.cache, "maxMoves": args.max_moves, "threads": args.threads,
            "leaseStartHex": f"0x{lease_start:08x}", "leaseEndExclusiveHex": f"0x{lease_end:08x}",
            "maxGenerations": args.max_generations, "wallHours": args.wall_hours,
            "sigmaFloor": args.sigma_floor, "cmaSeedHex": f"0x{args.cma_seed:08x}",
            "fitness": "mean whole-game score over the generation's block (higher is better)",
            "candidateRule": "distribution mean at the final generation",
        }, indent=2) + "\n")
        write_weights(run / "weights-frozen.txt", FROZEN, "frozen fair leaf (fast-leaf.hpp constants)")

    progress = run / "progress.jsonl"
    started = time.time()
    while True:
        if cma.gen >= args.max_generations:
            reason = "max-generations"; break
        if (time.time() - started) / 3600.0 > args.wall_hours:
            reason = "wall-hours"; break
        if cma.sigma < args.sigma_floor:
            reason = "sigma-floor"; break
        if (run / "STOP").exists():
            reason = "STOP file"; break
        gen = cma.gen
        seed_start = lease_start + gen * args.games
        if seed_start + args.games > lease_end:
            reason = "lease exhausted"; break

        # Reuse a generation's sample if it was drawn already (resume mid-evaluation).
        sample_path = run / f"gen-{gen:03d}" / "sample.npz"
        if sample_path.exists():
            loaded = np.load(sample_path)
            x, y = loaded["x"], loaded["y"]
            cma._B, cma._D = loaded["B"], loaded["D"]
        else:
            x, y = cma.ask()
            (run / f"gen-{gen:03d}").mkdir(exist_ok=True)
            np.savez(sample_path, x=x, y=y, B=cma._B, D=cma._D)

        t0 = time.time()
        art = run_generation(args, run, gen, x, seed_start)
        by_name = {ind["name"]: ind for ind in art["individuals"]}
        control = by_name["control"]
        control_scores = np.array([g["score"] for g in control["games"]], dtype=float)
        fitness = np.zeros(len(x))
        paired_sd = []
        for k in range(len(x)):
            ind = by_name[f"g{gen:03d}-i{k:02d}"]
            scores = np.array([g["score"] for g in ind["games"]], dtype=float)
            fitness[k] = scores.mean()
            paired_sd.append(float((scores - control_scores).std(ddof=1)))
        if art.get("incompleteDecisionsTotal", 0) != 0:
            raise SystemExit("generation artifact void: incomplete decisions")

        cma.tell(x, y, fitness)
        mean_w = decode(cma.m)
        write_weights(run / f"gen-{gen:03d}" / "mean-after.txt", mean_w, f"CMA mean after generation {gen}")
        write_weights(run / "candidate-mean-latest.txt", mean_w, f"CMA mean after generation {gen} (latest)")
        best = int(np.argmax(fitness))
        record = {
            "gen": gen, "seedStartHex": f"0x{seed_start:08x}", "games": args.games,
            "controlMean": float(control_scores.mean()),
            "populationMean": float(fitness.mean()),
            "populationBest": float(fitness[best]), "bestIndex": best,
            "populationWorst": float(fitness.min()),
            "meanPairedDeltaVsControl": float(fitness.mean() - control_scores.mean()),
            "medianPairedSdVsControl": float(np.median(paired_sd)),
            "sigma": cma.sigma, "condition": float(cma._D.max() / cma._D.min()),
            "meanWeights": mean_w.tolist(),
            "wallSeconds": time.time() - t0, "evaluatorWallSeconds": art["wallSeconds"],
        }
        with progress.open("a") as f:
            f.write(json.dumps(record) + "\n")
        state_path.write_text(json.dumps(cma.to_json()) + "\n")
        print(f"gen {gen:3d} seeds 0x{seed_start:08x}+{args.games}: control {control_scores.mean():9.0f} "
              f"pop mean {fitness.mean():9.0f} best {fitness[best]:9.0f} (delta {fitness.mean()-control_scores.mean():+8.0f}) "
              f"sigma {cma.sigma:.4f} paired-sd {np.median(paired_sd):8.0f} {time.time()-t0:6.0f}s", file=sys.stderr)

    final = {
        "stopReason": reason, "generationsCompleted": cma.gen, "sigma": cma.sigma,
        "candidateWeights": decode(cma.m).tolist(), "names": NAMES,
        "seedsConsumedEndExclusiveHex": f"0x{lease_start + cma.gen * args.games:08x}",
        "wallHours": (time.time() - started) / 3600.0,
    }
    (run / "final.json").write_text(json.dumps(final, indent=2) + "\n")
    write_weights(run / "candidate-weights.txt", decode(cma.m), f"frozen candidate: CMA mean after {cma.gen} generations; stop={reason}")
    print(f"stopped: {reason} after {cma.gen} generations", file=sys.stderr)


if __name__ == "__main__":
    main()
