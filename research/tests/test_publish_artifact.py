from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts/publish-research-artifact.sh"
RUN_ID = "RUN-20260101T000000Z-0123abcd"

# Stands in for the AWS CLI: answers head-object from the environment (so a
# publication can verify against a pretend object) and logs every call so a
# test can assert that a dry run touched nothing remote.
FAKE_AWS = """#!/usr/bin/env bash
printf '%s\\n' "$*" >> "${FAKE_AWS_LOG}"
if [[ "$1" == "s3api" && "$2" == "head-object" && -n "${FAKE_AWS_SHA256:-}" ]]; then
  printf '%s\\t%s\\n' "${FAKE_AWS_SHA256}" "${FAKE_AWS_BYTES}"
  exit 0
fi
exit 254
"""


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def reference(key: str, digest: str) -> str:
    return f"https://data.drop7.dev/runs/{RUN_ID}/{key}#sha256={digest}"


class PublishArtifactTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        fake_bin = self.root / "bin"
        fake_bin.mkdir()
        fake_aws = fake_bin / "aws"
        fake_aws.write_text(FAKE_AWS, encoding="utf-8")
        fake_aws.chmod(0o755)
        self.aws_log = self.root / "aws.log"
        self.env = {
            **os.environ,
            "PATH": f"{fake_bin}{os.pathsep}{os.environ.get('PATH', '')}",
            "FAKE_AWS_LOG": str(self.aws_log),
        }
        self.env.pop("FAKE_AWS_SHA256", None)
        self.env.pop("FAKE_AWS_BYTES", None)

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def publish(self, *arguments: str, **extra_env: str) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            ["bash", str(SCRIPT), "--run-id", RUN_ID, "--public", *arguments],
            capture_output=True,
            text=True,
            env={**self.env, **extra_env},
            cwd=self.root,
        )

    def test_dry_run_lists_objects_but_writes_no_manifest_line(self) -> None:
        source = self.root / "run" / "ntuple-scale"
        (source / "screen").mkdir(parents=True)
        rows = source / "screen" / "heldout.json"
        rows.write_text('{"games": 1}\n', encoding="utf-8")
        table = source / "best-weights.bin"
        table.write_bytes(b"\x00" * 64)
        manifest = self.root / "published.jsonl"
        existing = '{"key":"runs/other/x","bytes":1,"sha256":"0","ref":"r","status":"uploaded"}\n'
        manifest.write_text(existing, encoding="utf-8")

        completed = self.publish(
            "--dir", str(source), "--key", "ntuple-scale", "--exclude", "*.bin",
            "--manifest", str(manifest), "--dry-run",
        )

        self.assertEqual(completed.returncode, 0, completed.stderr)
        expected_ref = reference("ntuple-scale/screen/heldout.json", sha256(rows))
        self.assertEqual(
            completed.stdout.splitlines(),
            [f"dry-run {rows.stat().st_size} bytes {rows} -> {expected_ref}"],
        )
        self.assertIn("no line was written", completed.stderr)
        self.assertEqual(manifest.read_text(encoding="utf-8"), existing)
        self.assertFalse(self.aws_log.exists(), "a dry run must not call the AWS CLI")

    def test_publication_records_only_verified_objects(self) -> None:
        rows = self.root / "heldout.json"
        rows.write_text('{"games": 2}\n', encoding="utf-8")
        manifest = self.root / "published.jsonl"
        digest = sha256(rows)

        completed = self.publish(
            "--file", str(rows), "--manifest", str(manifest),
            FAKE_AWS_SHA256=digest, FAKE_AWS_BYTES=str(rows.stat().st_size),
        )

        self.assertEqual(completed.returncode, 0, completed.stderr)
        expected_ref = reference("heldout.json", digest)
        self.assertEqual(completed.stdout.splitlines(), [expected_ref])
        lines = [json.loads(line) for line in manifest.read_text(encoding="utf-8").splitlines()]
        self.assertEqual(
            lines,
            [{
                "key": f"runs/{RUN_ID}/heldout.json",
                "bytes": rows.stat().st_size,
                "sha256": digest,
                "ref": expected_ref,
                "status": "already-published",
            }],
        )
        self.assertNotIn("s3 cp", self.aws_log.read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
