from __future__ import annotations

import argparse
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / ".agents/skills/million-point-research/scripts/researchctl.py"
SPEC = importlib.util.spec_from_file_location("drop7_researchctl", SCRIPT)
assert SPEC is not None and SPEC.loader is not None
researchctl = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(researchctl)


class ResearchControlTests(unittest.TestCase):
    def test_generated_ids_have_valid_shape(self) -> None:
        theory_id = researchctl.dated_id("TH", "Sibling Value")
        run_id = researchctl.dated_id("RUN")
        self.assertRegex(
            theory_id,
            r"^TH-[0-9]{8}-sibling-value-[a-f0-9]{8}$",
        )
        self.assertRegex(
            run_id,
            r"^RUN-[0-9]{8}T[0-9]{6}Z-[a-f0-9]{8}$",
        )

    def test_all_templates_match_their_schemas(self) -> None:
        for path in sorted((ROOT / "research/templates").glob("*.json")):
            record = json.loads(path.read_text(encoding="utf-8"))
            schema_name = researchctl.RECORD_SCHEMAS[record["format"]]
            schema = json.loads(
                (ROOT / "research/schemas" / schema_name).read_text(encoding="utf-8")
            )
            errors = researchctl.schema_errors(record, schema, schema, path.name)
            self.assertEqual(errors, [], "\n".join(errors))

    def test_experiment_freeze_hash_excludes_only_hash_field(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "research/experiments").mkdir(parents=True)
            arguments = argparse.Namespace(
                slug="freeze-check",
                title="Freeze check",
                theory_id="TH-20260820-freeze-check-0123abcd",
                hypothesis="The frozen bytes remain auditable.",
                candidate="candidate",
                entry_point=None,
                classification="infrastructure",
                information_boundary="not-applicable",
                platform="test",
                model="test",
                agent_id=None,
            )
            path = researchctl.new_experiment(arguments, root)
            record = json.loads(path.read_text(encoding="utf-8"))
            record["metrics"]["primary"] = "Hash equality"
            record["gate"]["fixedBeforeControlledData"] = True
            record["gate"]["passCriteria"] = ["Canonical hash matches"]
            record["expectedArtifacts"] = ["Frozen protocol"]
            researchctl.write_json_atomic(path, record)

            with contextlib.redirect_stdout(io.StringIO()):
                result = researchctl.freeze_experiment(
                    argparse.Namespace(path=str(path.relative_to(root))), root
                )
            self.assertEqual(result, 0)
            frozen = json.loads(path.read_text(encoding="utf-8"))
            expected = researchctl.hashlib.sha256(
                researchctl.canonical_protocol_bytes(frozen)
            ).hexdigest()
            self.assertEqual(frozen["protocolSha256"], expected)
            self.assertEqual(frozen["lifecycle"], "preregistered")

    def test_exclusive_writer_refuses_overwrite(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "record.json"
            researchctl.write_json_exclusive(path, {"first": True})
            with self.assertRaises(FileExistsError):
                researchctl.write_json_exclusive(path, {"second": True})
            self.assertEqual(json.loads(path.read_text()), {"first": True})

    def test_machine_doctor_matches_machine_schema(self) -> None:
        profile = researchctl.machine_profile()
        schema = json.loads(
            (ROOT / "research/schemas/machine-v1.schema.json").read_text(encoding="utf-8")
        )
        errors = researchctl.schema_errors(profile, schema, schema, "machine")
        self.assertEqual(errors, [], "\n".join(errors))

    def test_commit_lint_requires_traceable_result_trailers(self) -> None:
        valid = """result(afterstate): record standard failure

Theory-ID: TH-20260820-afterstate-a1b2c3d4
Experiment-ID: EX-20260820-h40-83e712aa
Run-ID: RUN-20260820T184215Z-91b02c33
Result-ID: RS-20260820T190501Z-1e7a4c02
Contribution-ID: CT-20260820T184300Z-f482ab19
Evidence-Change: public-development-fail
Result-SHA256: 0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef
"""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            path = root / "message.txt"
            path.write_text(valid, encoding="utf-8")
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(
                    researchctl.commit_lint(argparse.Namespace(path=str(path)), root),
                    0,
                )
            path.write_text("docs(agent): explain records\n", encoding="utf-8")
            with contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(
                    researchctl.commit_lint(argparse.Namespace(path=str(path)), root),
                    1,
                )


if __name__ == "__main__":
    unittest.main()
