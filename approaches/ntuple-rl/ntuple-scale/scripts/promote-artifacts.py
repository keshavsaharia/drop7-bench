#!/usr/bin/env python3
"""Promote one ntuple-scale run's compact evidence from the ignored runs/
directory to artifacts/results/<experiment>/<run>/ with a content manifest.

Every regular file under runs/<run>/ntuple-scale/ is copied except table
files (*.bin, *.bin.zst, *.tmp), which stay on the workstation and are
published compressed to the research archive.  The manifest lists each
promoted file's path, source path, SHA-256 (of the copy, checked against the
source) and byte count, with a description derived from the file's name.
Files other than the manifest are hashed first; the manifest hashes itself
last, so the record can cite it.

Usage: promote-artifacts.py --root REPO --experiment EX-... --run RUN-... [--note TEXT]
"""
from __future__ import annotations

import argparse
import fnmatch
import hashlib
import json
import os
import shutil
import time

SKIP = ("*.bin", "*.bin.zst", "*.tmp", "*.zst")

DESCRIPTIONS = [
    ("gates.log", "CHECK gate lines on the probe block for the occ5 layout (all PASS)"),
    ("gates-hgt5.log", "CHECK gate lines on the probe block for the hgt5 layout (all PASS)"),
    ("main/gates-candidate.log", "CHECK gates re-run on the frozen fill candidate before the screen lease opened"),
    ("main/gates-control.log", "CHECK gates re-run on the frozen control candidate before the screen lease opened"),
    ("main/gates-zeroed.log", "CHECK gates re-run on the zeroed edit before the screen lease opened"),
    ("main/gates-classmean.log", "CHECK gates re-run on the class-mean edit before the screen lease opened"),
    ("main/edits.json", "report of the two no-training edits: entries at the starting value, entries changed, per-family counts by pattern occupancy, checkpoint cross-check"),
    ("main/edit.log", "stdout of edit-tables.py"),
    ("main/edit.err", "stderr of edit-tables.py"),
    ("main/touched-by-fill.txt", "seed-free diagnostic: touched entries of the first run's checkpoint by family, rise phase and pattern occupancy (the theory's mechanism numbers)"),
    ("main/touched-by-fill.json", "the same diagnostic, per table and phase, machine-readable"),
    ("main/touched-by-fill.err", "stderr of the diagnostic"),
    ("main/*-weights.sha256", "SHA-256 of a table file the screen played"),
    ("pilot/selection.json", "the fixed selection rule's output: candidate fill arm and control best point"),
    ("pilot/*/config.json", "training arm configuration (layout, warm start, seeds, validation, plateau rule)"),
    ("pilot/*/progress.jsonl", "training arm progress rows (one per chunk) with quick and validation readings"),
    ("pilot/*/val-*.json", "validation line-up population artifact at one point (ntuple-d3s7, ntuple-1ply, fair-d3s7 on the 256-game block)"),
    ("pilot/*/best.json", "which validation point the arm's best-weights.bin came from"),
    ("pilot/*/stop.json", "why the arm stopped"),
    ("pilot/*/train.log", "trainer stdout"),
    ("pilot/*/train.err", "trainer stderr (warm start, chunk and validation lines)"),
    ("pilot/*/DONE", "arm completion marker"),
    ("screen/heldout.json", "per-game rows of the one-shot held-out screen, every arm on identical seeds"),
    ("screen/compare-*.json", "compare.py paired report for one contrast (bootstrap, Student-t, floor, halves)"),
    ("screen.log", "screen binary stdout"),
    ("screen.err", "screen binary stderr (per-arm means and wall times)"),
    ("pipeline.log", "stage launch and completion log"),
    ("rusage.jsonl", "per-stage wall, CPU and peak-RSS rows"),
    ("prep.nohup", "stdout/stderr of the gates and edit launch"),
    ("chain.nohup", "stdout/stderr of the train -> select -> freeze -> screen -> compare -> analyze chain"),
    ("analysis.json", "analyze.py summary of every stage, including the gate and the readings beside it"),
    ("analysis.md", "the same summary as Markdown"),
    ("published.jsonl", "publisher manifest: every object uploaded to the public research archive with its digest and reference"),
]


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def describe(relative):
    for pattern, text in DESCRIPTIONS:
        if fnmatch.fnmatch(relative, pattern):
            return text
    return "run artifact"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", default=".")
    parser.add_argument("--experiment", required=True)
    parser.add_argument("--run", required=True)
    parser.add_argument("--note", default="Compact evidence promoted from the ignored runs/ directory. Every file is a byte copy of the run artifact named in sourcePath; sha256 is of the promoted copy and matches the source. Table files are retained on the workstation and published compressed to the public research archive (see the run record).")
    args = parser.parse_args()
    source_root = os.path.join(args.root, "runs", args.run, "ntuple-scale")
    dest_root = os.path.join(args.root, "artifacts", "results", args.experiment, args.run)
    files = []
    for directory, _, names in os.walk(source_root):
        for name in sorted(names):
            src = os.path.join(directory, name)
            relative = os.path.relpath(src, source_root)
            if any(fnmatch.fnmatch(name, s) for s in SKIP) or name == "manifest.json":
                continue
            dst = os.path.join(dest_root, relative)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copyfile(src, dst)
            digest = sha256_file(dst)
            if digest != sha256_file(src):
                raise SystemExit(f"copy mismatch for {relative}")
            files.append({
                "path": os.path.relpath(dst, args.root),
                "sourcePath": os.path.relpath(src, args.root),
                "sha256": digest,
                "bytes": os.path.getsize(dst),
                "description": describe(relative),
            })
    # The run-level publisher manifest sits beside the ntuple-scale directory.
    published = os.path.join(args.root, "runs", args.run, "published.jsonl")
    if os.path.isfile(published):
        dst = os.path.join(dest_root, "published.jsonl")
        shutil.copyfile(published, dst)
        files.append({"path": os.path.relpath(dst, args.root), "sourcePath": os.path.relpath(published, args.root), "sha256": sha256_file(dst), "bytes": os.path.getsize(dst), "description": describe("published.jsonl")})
    files.sort(key=lambda f: f["path"])
    manifest = {
        "format": "drop7-artifact-manifest-v1",
        "experimentId": args.experiment,
        "runId": args.run,
        "createdAt": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "note": args.note,
        "files": files,
    }
    path = os.path.join(dest_root, "manifest.json")
    with open(path, "w", encoding="utf-8") as handle:
        json.dump(manifest, handle, indent=2)
        handle.write("\n")
    print(f"{len(files)} files promoted; manifest {os.path.relpath(path, args.root)} sha256 {sha256_file(path)}")


if __name__ == "__main__":
    main()
