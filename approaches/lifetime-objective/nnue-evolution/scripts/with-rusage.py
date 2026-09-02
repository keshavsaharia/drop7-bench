#!/usr/bin/env python3
"""Run one command and append its resource usage as a JSON line.

GNU time is not installed on the research workstation, so this wrapper
records what the benchmark contract asks for from the kernel's own rusage
accounting: wall seconds, user and system CPU seconds, the peak resident set
of the largest child (ru_maxrss), and the exit code.  Stdout/stderr of the
child are passed through untouched; the JSON line goes to --record.

Usage: with-rusage.py --record FILE --label NAME -- cmd args...
"""
from __future__ import annotations

import argparse
import json
import resource
import subprocess
import sys
import time


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--record", required=True)
    parser.add_argument("--label", required=True)
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command
    if command and command[0] == "--":
        command = command[1:]
    if not command:
        parser.error("no command given")
    started_wall = time.time()
    started_mono = time.monotonic()
    before = resource.getrusage(resource.RUSAGE_CHILDREN)
    completed = subprocess.run(command)
    after = resource.getrusage(resource.RUSAGE_CHILDREN)
    row = {
        "label": args.label,
        "command": command,
        "startedAt": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime(started_wall)),
        "endedAt": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "wallSeconds": round(time.monotonic() - started_mono, 3),
        "userSeconds": round(after.ru_utime - before.ru_utime, 3),
        "systemSeconds": round(after.ru_stime - before.ru_stime, 3),
        # ru_maxrss is kilobytes on Linux; it is the peak of the largest
        # child, which for a single multi-threaded process is the process.
        "peakRssBytes": after.ru_maxrss * 1024,
        "exitCode": completed.returncode,
    }
    with open(args.record, "a", encoding="utf-8") as handle:
        handle.write(json.dumps(row) + "\n")
    return completed.returncode


if __name__ == "__main__":
    sys.exit(main())
