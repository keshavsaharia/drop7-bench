#!/usr/bin/env python3
"""Mark a held-out screen lease opened at stage D process start.

The decision to open a screen block exactly once, on the frozen candidate
whose SHA-256 is already on disk, is fixed in the experiment record; this
script only records the state transition the protocol requires (state opened,
openedAt, runIds, protocolSha256).  It refuses to run unless the candidate hash
file exists and is non-empty, the lease is still reserved, and the lease's
range starts at the seed the stage is about to read.

Usage: open-screen-lease.py --root REPO --run RUN_ID --hash-file PATH
                            [--lease research/seeds/leases/SL-....json]
                            [--experiment research/experiments/EX-....json]
                            [--seeds-start 0x...]
"""
from __future__ import annotations

import argparse
import json
import os
import time

DEFAULT_LEASE = "research/seeds/leases/SL-20260825T063000Z-a52e1300.json"
DEFAULT_EXPERIMENT = "research/experiments/EX-20260902-nnue-evolution-d3-v2-49c18bc2.json"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", required=True)
    parser.add_argument("--run", required=True)
    parser.add_argument("--hash-file", required=True)
    parser.add_argument("--lease", default=DEFAULT_LEASE)
    parser.add_argument("--experiment", default=DEFAULT_EXPERIMENT)
    parser.add_argument("--seeds-start", default=None)
    args = parser.parse_args()
    if not os.path.getsize(args.hash_file):
        raise SystemExit("candidate hash file is empty; refusing to open the screen lease")
    lease_path = os.path.join(args.root, args.lease)
    lease = json.load(open(lease_path, encoding="utf-8"))
    if lease["state"] != "reserved":
        raise SystemExit(f"screen lease is {lease['state']}, not reserved; refusing")
    if args.seeds_start is not None and int(args.seeds_start, 16) != int(lease["rangeStartHex"], 16):
        raise SystemExit(
            f"stage would read from {args.seeds_start} but the lease starts at {lease['rangeStartHex']}; refusing"
        )
    experiment = json.load(open(os.path.join(args.root, args.experiment), encoding="utf-8"))
    if lease.get("experimentId") != experiment["experimentId"]:
        raise SystemExit(
            f"lease belongs to {lease.get('experimentId')}, not {experiment['experimentId']}; refusing"
        )
    lease["state"] = "opened"
    lease["openedAt"] = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())
    lease["runIds"] = sorted(set(lease.get("runIds", []) + [args.run]))
    lease["protocolSha256"] = experiment["protocolSha256"]
    lease["notes"].append(
        f"Opened {lease['openedAt']} by run {args.run} for the one-shot screen "
        f"({lease['rangeStartHex']} to {lease['rangeEndExclusiveHex']} exclusive); "
        f"candidate SHA-256 recorded beforehand in {os.path.relpath(args.hash_file, args.root)}: "
        + open(args.hash_file, encoding="utf-8").read().split()[0]
    )
    with open(lease_path, "w", encoding="utf-8") as handle:
        json.dump(lease, handle, indent=2)
        handle.write("\n")
    print("opened", args.lease, lease["openedAt"])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
