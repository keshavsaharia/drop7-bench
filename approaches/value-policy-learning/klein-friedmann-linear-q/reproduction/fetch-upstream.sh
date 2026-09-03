#!/usr/bin/env bash
# Fetch Klein & Friedmann's CS221 code at the pinned commit into $1
# (default build/kf-upstream). The upstream repository carries no license, so
# it is never vendored into this tree; every run re-fetches it and records the
# commit and file hashes.
set -euo pipefail
DEST="${1:-build/kf-upstream}"
COMMIT=8cc8a0e
if [ ! -d "$DEST/.git" ]; then
  git clone -q https://github.com/ekreate/cs221-final-project.git "$DEST"
fi
git -C "$DEST" checkout -q "$COMMIT"
echo "commit $(git -C "$DEST" rev-parse HEAD)"
shasum -a 256 "$DEST/main.py" "$DEST/util.py" "$DEST/Drop7QLearning.py"
