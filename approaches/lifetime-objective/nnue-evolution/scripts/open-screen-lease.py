#!/usr/bin/env python3
"""Mark the held-out screen lease opened at stage D process start.

The decision to open this block exactly once, on the frozen candidate whose
SHA-256 is already on disk, was fixed in the experiment record's pre-data
amendment; this script only records the state transition the protocol
requires (state opened, openedAt, runIds, protocolSha256).  It refuses to run
unless the candidate hash file exists and the lease is still reserved.

Usage: open-screen-lease.py --root REPO --run RUN_ID --hash-file PATH
"""
from __future__ import annotations

import argparse
import json
import os
import time

LEASE = "research/seeds/leases/SL-20260825T063000Z-a52e1300.json"
EXPERIMENT = "research/experiments/EX-20260825-nnue-evolution-d3-bca7f330.json"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", required=True)
    parser.add_argument("--run", required=True)
    parser.add_argument("--hash-file", required=True)
    args = parser.parse_args()
    if not os.path.getsize(args.hash_file):
        raise SystemExit("candidate hash file is empty; refusing to open the screen lease")
    lease_path = os.path.join(args.root, LEASE)
    lease = json.load(open(lease_path, encoding="utf-8"))
    if lease["state"] != "reserved":
        raise SystemExit(f"screen lease is {lease['state']}, not reserved; refusing")
    experiment = json.load(open(os.path.join(args.root, EXPERIMENT), encoding="utf-8"))
    lease["state"] = "opened"
    lease["openedAt"] = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())
    lease["runIds"] = sorted(set(lease.get("runIds", []) + [args.run]))
    lease["protocolSha256"] = experiment["protocolSha256"]
    lease["notes"].append(
        f"Opened {lease['openedAt']} by run {args.run} for the one-shot 64-game screen (0xa52e1300-0xa52e133f); "
        f"candidate SHA-256 recorded beforehand in {os.path.relpath(args.hash_file, args.root)}: "
        + open(args.hash_file, encoding="utf-8").read().split()[0]
    )
    with open(lease_path, "w", encoding="utf-8") as handle:
        json.dump(lease, handle, indent=2)
        handle.write("\n")
    print("opened", LEASE, lease["openedAt"])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
