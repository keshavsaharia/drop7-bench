#!/usr/bin/env python3
"""Dependency-free helpers for the Drop7 research record workflow."""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import secrets
import shutil
import subprocess
import sys
from typing import Any, Iterable


RECORD_SCHEMAS = {
    "drop7-theory-v1": "theory-v1.schema.json",
    "drop7-experiment-v1": "experiment-v1.schema.json",
    "drop7-run-v1": "run-v1.schema.json",
    "drop7-result-v1": "result-v1.schema.json",
    "drop7-game-result-v1": "game-result-v1.schema.json",
    "drop7-contribution-v1": "contribution-v1.schema.json",
    "drop7-machine-v1": "machine-v1.schema.json",
    "drop7-dataset-v1": "dataset-v1.schema.json",
    "drop7-seed-lease-v1": "seed-lease-v1.schema.json",
}

LIVE_RECORD_DIRS = {
    "theories": "drop7-theory-v1",
    "experiments": "drop7-experiment-v1",
    "runs": "drop7-run-v1",
    "results": "drop7-result-v1",
    "contributions": "drop7-contribution-v1",
    "datasets": "drop7-dataset-v1",
    "system-profiles": "drop7-machine-v1",
}

ID_FIELDS = {
    "drop7-theory-v1": "theoryId",
    "drop7-experiment-v1": "experimentId",
    "drop7-run-v1": "runId",
    "drop7-result-v1": "resultId",
    "drop7-contribution-v1": "contributionId",
    "drop7-machine-v1": "machineProfileId",
    "drop7-dataset-v1": "datasetId",
    "drop7-seed-lease-v1": "seedLeaseId",
}


def repository_root() -> Path:
    candidates = [Path.cwd(), *Path(__file__).resolve().parents]
    for candidate in candidates:
        if (
            (candidate / "package.json").is_file()
            and (candidate / "docs/methodology.md").is_file()
            and (candidate / "research/schemas").is_dir()
        ):
            return candidate
    raise RuntimeError("run from the Drop7 repository or keep the script inside it")


def utc_now() -> dt.datetime:
    return dt.datetime.now(dt.timezone.utc).replace(microsecond=0)


def iso_time(value: dt.datetime | None = None) -> str:
    return (value or utc_now()).isoformat().replace("+00:00", "Z")


def safe_slug(value: str) -> str:
    slug = re.sub(r"[^a-z0-9]+", "-", value.lower()).strip("-")
    if not slug or len(slug) > 64:
        raise ValueError("slug must contain 1-64 lowercase letters, digits, or hyphens")
    return slug


def dated_id(prefix: str, slug: str | None = None) -> str:
    now = utc_now()
    suffix = secrets.token_hex(4)
    if slug is None:
        return f"{prefix}-{now:%Y%m%dT%H%M%SZ}-{suffix}"
    return f"{prefix}-{now:%Y%m%d}-{safe_slug(slug)}-{suffix}"


def write_json_exclusive(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    encoded = json.dumps(value, indent=2, sort_keys=False, ensure_ascii=False) + "\n"
    with path.open("x", encoding="utf-8") as output:
        output.write(encoded)


def write_json_atomic(path: Path, value: Any) -> None:
    encoded = json.dumps(value, indent=2, sort_keys=False, ensure_ascii=False) + "\n"
    temporary = path.with_name(f".{path.name}.{secrets.token_hex(4)}.tmp")
    with temporary.open("x", encoding="utf-8") as output:
        output.write(encoded)
        output.flush()
        os.fsync(output.fileno())
    os.replace(temporary, path)


def actor(args: argparse.Namespace) -> dict[str, Any]:
    return {
        "platform": args.platform,
        "model": args.model,
        "agentId": args.agent_id,
    }


def new_theory(args: argparse.Namespace, root: Path) -> Path:
    record_id = dated_id("TH", args.slug)
    now = iso_time()
    record = {
        "$schema": "../schemas/theory-v1.schema.json",
        "format": "drop7-theory-v1",
        "theoryId": record_id,
        "title": args.title,
        "claim": args.claim or f"Draft claim: {args.title}",
        "mechanism": args.mechanism or "Specify the mechanism before advancing beyond draft.",
        "falsificationCriteria": args.falsification
        or ["Specify a quantitative falsification criterion before preregistration."],
        "informationClass": args.information_class,
        "lifecycle": "draft",
        "assessment": "untested",
        "evidenceTier": "proposal",
        "dependencies": [],
        "evidenceRefs": [],
        "createdBy": actor(args),
        "createdAt": now,
        "updatedAt": now,
    }
    path = root / "research/theories" / f"{record_id}.json"
    write_json_exclusive(path, record)
    return path


def new_experiment(args: argparse.Namespace, root: Path) -> Path:
    record_id = dated_id("EX", args.slug)
    now = iso_time()
    record = {
        "$schema": "../schemas/experiment-v1.schema.json",
        "format": "drop7-experiment-v1",
        "experimentId": record_id,
        "theoryIds": [args.theory_id],
        "title": args.title,
        "classification": args.classification,
        "hypothesis": args.hypothesis or f"Draft hypothesis: {args.title}",
        "candidate": {
            "name": args.candidate,
            "entryPoint": args.entry_point,
            "manifestRef": None,
        },
        "comparator": {
            "name": "fair-d4",
            "entryPoint": "approaches/fair-expectimax/reference/fair-only-depth4.cpp",
            "manifestRef": None,
        },
        "informationBoundary": args.information_boundary,
        "benchmarkTier": "CHECK",
        "data": {
            "role": "no-gameplay",
            "seedLeaseRefs": [],
            "datasetRefs": [],
            "wholeOriginSplit": True,
            "reuseDisclosure": "No gameplay data is read by this draft.",
        },
        "metrics": {
            "primary": "Specify before preregistration.",
            "secondary": [],
            "statisticalUnit": "not-applicable",
            "uncertaintyMethod": "not-applicable",
        },
        "gate": {
            "fixedBeforeControlledData": False,
            "passCriteria": ["Specify before preregistration."],
            "failureAction": "Record the exact configuration and open no later cohort.",
            "passAction": "Advance only to the named next tier without changing the candidate.",
        },
        "resources": {
            "wallSeconds": None,
            "cpuThreads": None,
            "maxHostBytes": None,
            "maxGpuBytes": None,
            "gpuDevices": [],
        },
        "stopConditions": [
            "Stop on any rules, information-boundary, legality, determinism, or parity failure."
        ],
        "expectedArtifacts": ["Specify before preregistration."],
        "lifecycle": "draft",
        "protocolSha256": None,
        "amendments": [],
        "createdBy": actor(args),
        "createdAt": now,
        "updatedAt": now,
    }
    path = root / "research/experiments" / f"{record_id}.json"
    write_json_exclusive(path, record)
    return path


def new_contribution(args: argparse.Namespace, root: Path) -> Path:
    record_id = dated_id("CT")
    record = {
        "$schema": "../schemas/contribution-v1.schema.json",
        "format": "drop7-contribution-v1",
        "contributionId": record_id,
        "actor": {
            "kind": args.actor_kind,
            "platform": args.platform,
            "model": args.model,
            "agentId": args.agent_id,
        },
        "level": args.level,
        "roles": [{"role": args.role, "degree": args.degree}],
        "summary": args.summary,
        "theoryIds": args.theory_id,
        "experimentIds": args.experiment_id,
        "runIds": args.run_id,
        "artifactPaths": args.artifact,
        "commitShas": [],
        "validationPerformed": args.validation,
        "limitations": ["Self-reported contribution; not yet independently reviewed."],
        "selfReported": True,
        "reviewedBy": None,
        "recordedAt": iso_time(),
    }
    path = root / "research/contributions" / f"{record_id}.json"
    write_json_exclusive(path, record)
    return path


def canonical_protocol_bytes(record: dict[str, Any]) -> bytes:
    canonical = dict(record)
    canonical["protocolSha256"] = None
    return (json.dumps(canonical, sort_keys=True, separators=(",", ":")) + "\n").encode()


def freeze_experiment(args: argparse.Namespace, root: Path) -> int:
    resolved_root = root.resolve()
    path = (resolved_root / args.path).resolve() if not Path(args.path).is_absolute() else Path(args.path).resolve()
    try:
        path.relative_to(resolved_root)
    except ValueError as error:
        raise ValueError("experiment must be inside the repository") from error
    record = json.loads(path.read_text(encoding="utf-8"))
    if record.get("format") != "drop7-experiment-v1":
        raise ValueError("freeze accepts a drop7-experiment-v1 record")
    if record.get("lifecycle") != "draft":
        raise ValueError("only a draft experiment can be frozen")
    if not record.get("gate", {}).get("fixedBeforeControlledData"):
        raise ValueError("set gate.fixedBeforeControlledData to true before freezing")
    encoded = json.dumps(record)
    if "Specify before preregistration" in encoded or "Draft hypothesis:" in encoded:
        raise ValueError("replace all draft protocol fields before freezing")
    record["lifecycle"] = "preregistered"
    record["updatedAt"] = iso_time()
    record["protocolSha256"] = None
    record["protocolSha256"] = hashlib.sha256(canonical_protocol_bytes(record)).hexdigest()
    write_json_atomic(path, record)
    print(f"frozen {path.relative_to(resolved_root)} {record['protocolSha256']}")
    return 0


def command_output(arguments: list[str], timeout: float = 5.0) -> tuple[str | None, str | None]:
    if shutil.which(arguments[0]) is None:
        return None, f"{arguments[0]} not found"
    try:
        process = subprocess.run(
            arguments,
            check=False,
            capture_output=True,
            text=True,
            timeout=timeout,
            env={**os.environ, "LC_ALL": "C"},
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        return None, str(error)
    text = (process.stdout + "\n" + process.stderr).strip()
    if process.returncode != 0:
        return None, f"exit {process.returncode}: {text[:300]}"
    return text, None


def read_first(path: Path) -> str | None:
    try:
        return path.read_text(encoding="utf-8").strip()
    except (OSError, UnicodeError):
        return None


def linux_meminfo() -> dict[str, int]:
    values: dict[str, int] = {}
    text = read_first(Path("/proc/meminfo"))
    if text is None:
        return values
    for line in text.splitlines():
        match = re.match(r"^([^:]+):\s+([0-9]+)\s+kB$", line)
        if match:
            values[match.group(1)] = int(match.group(2)) * 1024
    return values


def os_release() -> str | None:
    text = read_first(Path("/etc/os-release"))
    if text is None:
        return None
    values: dict[str, str] = {}
    for line in text.splitlines():
        if "=" in line:
            key, value = line.split("=", 1)
            values[key] = value.strip().strip('"')
    return values.get("PRETTY_NAME")


def detect_container() -> str | None:
    if Path("/.dockerenv").exists():
        return "docker-compatible"
    if Path("/run/.containerenv").exists():
        return "containerenv-compatible"
    text = read_first(Path("/proc/1/cgroup")) or ""
    for marker in ("kubepods", "docker", "podman", "lxc"):
        if marker in text.lower():
            return marker
    return None


def parse_lscpu() -> dict[str, str]:
    output, _ = command_output(["lscpu", "-J"])
    if output is None:
        return {}
    try:
        rows = json.loads(output).get("lscpu", [])
    except json.JSONDecodeError:
        return {}
    return {str(row.get("field", "")).rstrip(":"): str(row.get("data", "")) for row in rows}


def positive_int(value: str | None) -> int | None:
    try:
        number = int(value or "")
        return number if number > 0 else None
    except ValueError:
        return None


def detect_cpu() -> dict[str, Any]:
    fields = parse_lscpu()
    sockets = positive_int(fields.get("Socket(s)"))
    cores_per_socket = positive_int(fields.get("Core(s) per socket"))
    physical = sockets * cores_per_socket if sockets and cores_per_socket else None
    if platform.system() == "Darwin":
        model, _ = command_output(["sysctl", "-n", "machdep.cpu.brand_string"])
        physical_text, _ = command_output(["sysctl", "-n", "hw.physicalcpu"])
        physical = positive_int(physical_text)
    else:
        model = fields.get("Model name")
    try:
        affinity = len(os.sched_getaffinity(0))
        affinity_text = ",".join(str(value) for value in sorted(os.sched_getaffinity(0)))
    except AttributeError:
        affinity = None
        affinity_text = None
    governor = read_first(Path("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor"))
    return {
        "model": model.strip() if isinstance(model, str) and model.strip() else None,
        "physicalCores": physical,
        "logicalCpus": os.cpu_count() or 1,
        "affinityLogicalCpus": affinity,
        "numaNodes": positive_int(fields.get("NUMA node(s)")),
        "governor": governor,
        "affinityText": affinity_text,
    }


def detect_memory() -> dict[str, int | None]:
    info = linux_meminfo()
    total = info.get("MemTotal")
    available = info.get("MemAvailable")
    swap = info.get("SwapTotal")
    if platform.system() == "Darwin":
        total_text, _ = command_output(["sysctl", "-n", "hw.memsize"])
        total = positive_int(total_text)
    try:
        page_size = os.sysconf("SC_PAGE_SIZE")
    except (AttributeError, ValueError):
        page_size = None
    return {
        "totalBytes": total,
        "availableBytes": available,
        "swapTotalBytes": swap,
        "pageSizeBytes": page_size,
    }


def numeric_limit(text: str | None) -> int | None:
    if text is None or text == "max":
        return None
    try:
        value = int(text)
        return value if value >= 0 else None
    except ValueError:
        return None


def detect_limits(cpu_affinity: str | None) -> dict[str, Any]:
    cgroup_v2 = Path("/sys/fs/cgroup/cgroup.controllers").exists()
    memory_max = read_first(Path("/sys/fs/cgroup/memory.max")) if cgroup_v2 else None
    cpu_max = read_first(Path("/sys/fs/cgroup/cpu.max")) if cgroup_v2 else None
    cpuset = read_first(Path("/sys/fs/cgroup/cpuset.cpus.effective")) if cgroup_v2 else None
    return {
        "cgroupVersion": "v2" if cgroup_v2 else None,
        "memoryMaxBytes": numeric_limit(memory_max),
        "cpuQuota": cpu_max,
        "cpuAffinity": cpuset or cpu_affinity,
    }


def version_line(command: list[str]) -> str | None:
    output, _ = command_output(command)
    if not output:
        return None
    return output.splitlines()[0].strip()[:500]


def detect_gpu() -> tuple[dict[str, Any], list[str]]:
    notes: list[str] = []
    devices: list[dict[str, Any]] = []
    targets: list[str] = []
    rocminfo, rocminfo_error = command_output(["rocminfo"], timeout=10.0)
    if rocminfo:
        targets = sorted(set(re.findall(r"\bgfx[0-9a-z]+\b", rocminfo)))
        names = [name.strip() for name in re.findall(r"Marketing Name:\s*([^\n]+)", rocminfo)]
        for index, target in enumerate(targets):
            name = names[index] if index < len(names) and names[index] else target
            devices.append({"name": name, "target": target, "memoryBytes": None})
    elif rocminfo_error:
        notes.append(rocminfo_error)

    torch_script = (
        "import json, torch; "
        "print(json.dumps({'version':torch.__version__, 'hip':torch.version.hip, "
        "'available':torch.cuda.is_available(), "
        "'devices':[torch.cuda.get_device_name(i) for i in range(torch.cuda.device_count())]}))"
    )
    torch_output, torch_error = command_output(["python3", "-c", torch_script], timeout=15.0)
    torch_info: dict[str, Any] = {}
    if torch_output:
        try:
            torch_info = json.loads(torch_output.splitlines()[-1])
        except json.JSONDecodeError:
            notes.append("PyTorch GPU probe returned non-JSON output")
    elif torch_error:
        notes.append(f"PyTorch probe: {torch_error}")

    if not devices:
        for name in torch_info.get("devices", []):
            devices.append({"name": str(name), "target": None, "memoryBytes": None})

    rocm_version = read_first(Path("/opt/rocm/.info/version"))
    if rocm_version is None:
        rocm_version = version_line(["rocminfo", "--version"])
    hip_version = torch_info.get("hip") or version_line(["hipcc", "--version"])
    shared = None
    notes.append("Confirm integrated/shared-memory status from the exact detected hardware.")
    return (
        {
            "devices": devices,
            "rocmVersion": rocm_version,
            "hipVersion": str(hip_version) if hip_version else None,
            "sharedSystemMemory": shared,
            "detectionNotes": "; ".join(notes),
        },
        [
            f"PyTorch {torch_info.get('version')}" if torch_info.get("version") else "PyTorch unavailable or not importable"
        ],
    )


def machine_profile() -> dict[str, Any]:
    cpu = detect_cpu()
    affinity_text = cpu.pop("affinityText")
    gpu, gpu_notes = detect_gpu()
    toolchain = {
        "python": version_line(["python3", "--version"]),
        "node": version_line(["node", "--version"]),
        "clang": version_line(["clang++", "--version"]),
        "gcc": version_line(["g++", "--version"]),
        "make": version_line(["make", "--version"]),
        "cmake": version_line(["cmake", "--version"]),
        "hipcc": version_line(["hipcc", "--version"]),
    }
    return {
        "$schema": "../schemas/machine-v1.schema.json",
        "format": "drop7-machine-v1",
        "machineProfileId": dated_id("MACH"),
        "capturedAt": iso_time(),
        "operatingSystem": {
            "system": platform.system(),
            "release": platform.release(),
            "version": platform.version(),
            "architecture": platform.machine(),
            "distribution": os_release(),
            "container": detect_container(),
        },
        "cpu": cpu,
        "memory": detect_memory(),
        "limits": detect_limits(affinity_text),
        "gpu": gpu,
        "toolchain": toolchain,
        "profilers": {
            "perf": shutil.which("perf") is not None,
            "rocprofv3": shutil.which("rocprofv3") is not None,
            "rocm-smi": shutil.which("rocm-smi") is not None,
            "numactl": shutil.which("numactl") is not None,
        },
        "notes": gpu_notes,
    }


def doctor(args: argparse.Namespace, root: Path) -> int:
    profile = machine_profile()
    encoded = json.dumps(profile, indent=2, ensure_ascii=False) + "\n"
    if args.output is None:
        sys.stdout.write(encoded)
        return 0
    output = Path(args.output)
    if not output.is_absolute():
        output = root / output
    if output.exists() and output.is_dir():
        output = output / f"{profile['machineProfileId']}.json"
    write_json_exclusive(output, profile)
    print(output.relative_to(root) if output.is_relative_to(root) else output)
    return 0


def resolve_pointer(root_schema: dict[str, Any], pointer: str) -> dict[str, Any]:
    if not pointer.startswith("#/"):
        raise ValueError(f"unsupported schema reference {pointer}")
    value: Any = root_schema
    for part in pointer[2:].split("/"):
        value = value[part.replace("~1", "/").replace("~0", "~")]
    if not isinstance(value, dict):
        raise ValueError(f"schema reference is not an object: {pointer}")
    return value


def type_matches(value: Any, expected: str) -> bool:
    if expected == "null":
        return value is None
    if expected == "object":
        return isinstance(value, dict)
    if expected == "array":
        return isinstance(value, list)
    if expected == "string":
        return isinstance(value, str)
    if expected == "boolean":
        return isinstance(value, bool)
    if expected == "integer":
        return isinstance(value, int) and not isinstance(value, bool)
    if expected == "number":
        return isinstance(value, (int, float)) and not isinstance(value, bool)
    return True


def schema_errors(value: Any, schema: dict[str, Any], root_schema: dict[str, Any], where: str) -> list[str]:
    if "$ref" in schema:
        schema = resolve_pointer(root_schema, schema["$ref"])
    errors: list[str] = []
    expected = schema.get("type")
    if expected is not None:
        types = [expected] if isinstance(expected, str) else expected
        if not any(type_matches(value, item) for item in types):
            return [f"{where}: expected {' or '.join(types)}, got {type(value).__name__}"]
    if "const" in schema and value != schema["const"]:
        errors.append(f"{where}: expected constant {schema['const']!r}")
    if "enum" in schema and value not in schema["enum"]:
        errors.append(f"{where}: {value!r} is not an allowed value")
    if isinstance(value, str):
        if len(value) < schema.get("minLength", 0):
            errors.append(f"{where}: string is too short")
        if "pattern" in schema and re.fullmatch(schema["pattern"], value) is None:
            errors.append(f"{where}: does not match {schema['pattern']}")
        if schema.get("format") == "date-time":
            try:
                dt.datetime.fromisoformat(value.replace("Z", "+00:00"))
            except ValueError:
                errors.append(f"{where}: invalid date-time")
    if isinstance(value, (int, float)) and not isinstance(value, bool):
        if "minimum" in schema and value < schema["minimum"]:
            errors.append(f"{where}: below minimum {schema['minimum']}")
        if "exclusiveMinimum" in schema and value <= schema["exclusiveMinimum"]:
            errors.append(f"{where}: not above {schema['exclusiveMinimum']}")
    if isinstance(value, list):
        if len(value) < schema.get("minItems", 0):
            errors.append(f"{where}: has too few items")
        if schema.get("uniqueItems"):
            encoded = [json.dumps(item, sort_keys=True) for item in value]
            if len(encoded) != len(set(encoded)):
                errors.append(f"{where}: items must be unique")
        if isinstance(schema.get("items"), dict):
            for index, item in enumerate(value):
                errors.extend(schema_errors(item, schema["items"], root_schema, f"{where}[{index}]"))
    if isinstance(value, dict):
        for required in schema.get("required", []):
            if required not in value:
                errors.append(f"{where}: missing required key {required}")
        properties = schema.get("properties", {})
        for key, item in value.items():
            if key in properties:
                errors.extend(schema_errors(item, properties[key], root_schema, f"{where}.{key}"))
            elif schema.get("additionalProperties") is False:
                errors.append(f"{where}: unexpected key {key}")
            elif isinstance(schema.get("additionalProperties"), dict):
                errors.extend(
                    schema_errors(item, schema["additionalProperties"], root_schema, f"{where}.{key}")
                )
    for subschema in schema.get("allOf", []):
        condition = subschema.get("if")
        if condition is None or not schema_errors(value, condition, root_schema, where):
            if "then" in subschema:
                errors.extend(schema_errors(value, subschema["then"], root_schema, where))
    return errors


def load_json(path: Path) -> Any:
    return json.loads(path.read_text(encoding="utf-8"))


def live_record_files(root: Path) -> list[Path]:
    paths: list[Path] = []
    for directory in LIVE_RECORD_DIRS:
        paths.extend(sorted((root / "research" / directory).glob("*.json")))
    paths.extend(sorted((root / "research/seeds/leases").glob("*.json")))
    return paths


def references_in(record: dict[str, Any]) -> Iterable[str]:
    for key in (
        "theoryIds",
        "experimentIds",
        "runIds",
        "contributionIds",
        "seedLeaseRefs",
        "datasetRefs",
        "parentDatasetRefs",
    ):
        for value in record.get(key, []):
            if isinstance(value, str):
                yield value
    if isinstance(record.get("experimentId"), str):
        yield record["experimentId"]
    for key in ("data",):
        nested = record.get(key)
        if isinstance(nested, dict):
            yield from references_in(nested)


def validate_markdown_links(root: Path) -> list[str]:
    errors: list[str] = []
    link = re.compile(r"(?<!!)\[[^\]]+\]\(([^)]+)\)")
    excluded = {".git", "build", "node_modules", "runs"}
    paths = [
        path
        for path in root.rglob("*.md")
        if not any(
            part in excluded or part.startswith(".venv")
            for part in path.relative_to(root).parts
        )
    ]
    for path in sorted(paths):
        if not path.is_file():
            continue
        for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            for raw in link.findall(line):
                target = raw.strip().split(" ", 1)[0].strip("<>")
                if not target or target.startswith(("http://", "https://", "mailto:", "#")):
                    continue
                file_part = target.split("#", 1)[0]
                resolved = (path.parent / file_part).resolve()
                if not resolved.exists():
                    errors.append(f"{path.relative_to(root)}:{line_number}: missing link target {file_part}")
    return errors


def validate_records(root: Path) -> list[str]:
    errors: list[str] = []
    schemas: dict[str, dict[str, Any]] = {}
    for record_format, filename in RECORD_SCHEMAS.items():
        path = root / "research/schemas" / filename
        try:
            schemas[record_format] = load_json(path)
        except (OSError, json.JSONDecodeError) as error:
            errors.append(f"{path.relative_to(root)}: invalid schema JSON: {error}")

    for path in sorted((root / "research/templates").glob("*.json")):
        try:
            record = load_json(path)
        except json.JSONDecodeError as error:
            errors.append(f"{path.relative_to(root)}: invalid JSON: {error}")
            continue
        record_format = record.get("format") if isinstance(record, dict) else None
        if record_format in schemas:
            errors.extend(schema_errors(record, schemas[record_format], schemas[record_format], str(path.relative_to(root))))

    records: list[tuple[Path, dict[str, Any]]] = []
    ids: dict[str, Path] = {}
    for path in live_record_files(root):
        try:
            record = load_json(path)
        except json.JSONDecodeError as error:
            errors.append(f"{path.relative_to(root)}: invalid JSON: {error}")
            continue
        if not isinstance(record, dict):
            errors.append(f"{path.relative_to(root)}: record must be an object")
            continue
        record_format = record.get("format")
        schema = schemas.get(record_format)
        if schema is None:
            errors.append(f"{path.relative_to(root)}: unknown format {record_format!r}")
            continue
        errors.extend(schema_errors(record, schema, schema, str(path.relative_to(root))))
        if "REPLACE_" in json.dumps(record):
            errors.append(f"{path.relative_to(root)}: live record contains a REPLACE_ placeholder")
        record_id = record.get(ID_FIELDS[record_format])
        if isinstance(record_id, str):
            if record_id in ids:
                errors.append(
                    f"{path.relative_to(root)}: duplicate ID also in {ids[record_id].relative_to(root)}"
                )
            ids[record_id] = path
            if path.stem != record_id:
                errors.append(f"{path.relative_to(root)}: filename must be {record_id}.json")
        records.append((path, record))

    for path, record in records:
        for reference in references_in(record):
            if re.match(r"^(TH|EX|RUN|RS|CT|DS|SL|MACH)-", reference) and reference not in ids:
                errors.append(f"{path.relative_to(root)}: missing referenced record {reference}")
        if record.get("format") == "drop7-experiment-v1" and record.get("lifecycle") in {
            "preregistered",
            "running",
            "completed",
        }:
            if not record.get("gate", {}).get("fixedBeforeControlledData"):
                errors.append(f"{path.relative_to(root)}: frozen experiment gate is not fixed")
            observed = record.get("protocolSha256")
            expected = hashlib.sha256(canonical_protocol_bytes(record)).hexdigest()
            if observed != expected:
                errors.append(f"{path.relative_to(root)}: protocolSha256 does not match canonical content")
        if record.get("format") == "drop7-result-v1":
            validity = record.get("runValidity")
            outcome = record.get("scientificOutcome")
            if validity == "invalid" and outcome not in {"inconclusive", "not-applicable"}:
                errors.append(f"{path.relative_to(root)}: an invalid run cannot pass or fail a hypothesis")
            if record.get("evidenceTier") in {"protected-validation", "final-confirmation"} and validity != "valid":
                errors.append(f"{path.relative_to(root)}: protected/final evidence requires a valid run")
            per_game = record.get("perGameArtifact", {})
            artifact_path = per_game.get("path")
            if artifact_path:
                resolved = root / artifact_path
                if not resolved.is_file():
                    errors.append(f"{path.relative_to(root)}: missing per-game artifact {artifact_path}")
                elif hashlib.sha256(resolved.read_bytes()).hexdigest() != per_game.get("sha256"):
                    errors.append(f"{path.relative_to(root)}: per-game artifact hash mismatch")
            for machine_ref in record.get("machineProfileRefs", []):
                if not (root / machine_ref).is_file():
                    errors.append(f"{path.relative_to(root)}: missing machine profile {machine_ref}")
        if record.get("format") == "drop7-experiment-v1":
            for policy_name in ("candidate", "comparator"):
                entry_point = record.get(policy_name, {}).get("entryPoint")
                if entry_point and not (root / entry_point).is_file():
                    errors.append(f"{path.relative_to(root)}: missing {policy_name} entry point {entry_point}")
        if record.get("format") == "drop7-run-v1":
            machine_ref = record.get("machineProfileRef")
            if machine_ref and not (root / machine_ref).is_file():
                errors.append(f"{path.relative_to(root)}: missing machine profile {machine_ref}")
        if record.get("format") == "drop7-contribution-v1":
            for artifact_path in record.get("artifactPaths", []):
                if not (root / artifact_path).exists():
                    errors.append(f"{path.relative_to(root)}: missing attributed artifact {artifact_path}")
        if record.get("format") == "drop7-dataset-v1":
            artifact = record.get("artifact", {})
            artifact_path = artifact.get("path")
            if artifact_path:
                resolved = root / artifact_path
                if not resolved.is_file():
                    errors.append(f"{path.relative_to(root)}: missing dataset artifact {artifact_path}")
                elif hashlib.sha256(resolved.read_bytes()).hexdigest() != artifact.get("sha256"):
                    errors.append(f"{path.relative_to(root)}: dataset artifact hash mismatch")

    leases = [(path, record) for path, record in records if record.get("format") == "drop7-seed-lease-v1"]
    active: list[tuple[Path, dict[str, Any], int, int]] = []
    for path, lease in leases:
        if lease.get("state") in {"draft", "cancelled-unopened"}:
            continue
        try:
            start = int(lease["rangeStartHex"], 16)
            end = int(lease["rangeEndExclusiveHex"], 16)
        except (TypeError, ValueError):
            errors.append(f"{path.relative_to(root)}: active range lease needs hexadecimal bounds")
            continue
        if start >= end:
            errors.append(f"{path.relative_to(root)}: seed range is empty or reversed")
        if lease.get("role") == "protected" and not (0x7D000000 <= start < end <= 0x7D010000):
            errors.append(f"{path.relative_to(root)}: protected lease is outside the frozen bank")
        if lease.get("role") == "final" and not (0xD7000000 <= start < end <= 0xD7000100):
            errors.append(f"{path.relative_to(root)}: final lease is outside the frozen bank")
        if lease.get("state") in {"opened", "burned"} and lease.get("openedAt") is None:
            errors.append(f"{path.relative_to(root)}: opened/burned lease lacks openedAt")
        active.append((path, lease, start, end))
    for index, (left_path, left, left_start, left_end) in enumerate(active):
        for right_path, right, right_start, right_end in active[index + 1 :]:
            if left.get("rngAlgorithm") != right.get("rngAlgorithm") or left.get("domain") != right.get("domain"):
                continue
            if max(left_start, right_start) < min(left_end, right_end):
                errors.append(
                    f"{left_path.relative_to(root)} and {right_path.relative_to(root)}: overlapping active seed leases"
                )
    return errors


def validate(args: argparse.Namespace, root: Path) -> int:
    errors = validate_records(root)
    errors.extend(validate_markdown_links(root))
    try:
        tiers = load_json(root / "research/benchmarks/tiers-v1.json")
        names = [row.get("name") for row in tiers.get("tiers", [])]
        if names != ["CHECK", "PILOT", "SCREEN", "STANDARD", "QUALIFY", "PROTECTED", "FINAL"]:
            errors.append("research/benchmarks/tiers-v1.json: unexpected tier order")
    except (OSError, json.JSONDecodeError) as error:
        errors.append(f"research/benchmarks/tiers-v1.json: {error}")
    try:
        profiles = load_json(root / "research/benchmarks/profiles-v1.json")
        names = [row.get("name") for row in profiles.get("profiles", [])]
        expected = ["correctness", "fixed-work-strength", "machine-max-throughput", "resource-frontier"]
        if names != expected:
            errors.append("research/benchmarks/profiles-v1.json: unexpected profile order")
    except (OSError, json.JSONDecodeError) as error:
        errors.append(f"research/benchmarks/profiles-v1.json: {error}")
    try:
        baselines = load_json(root / "research/benchmarks/baselines-v1.json")
        if baselines.get("format") != "drop7-baselines-v1":
            errors.append("research/benchmarks/baselines-v1.json: unexpected format")
        for source in baselines.get("primaryComparator", {}).get("sourceClosure", []):
            source_path = root / source.get("path", "")
            if not source_path.is_file():
                errors.append(
                    f"research/benchmarks/baselines-v1.json: missing source {source.get('path')}"
                )
                continue
            observed = hashlib.sha256(source_path.read_bytes()).hexdigest()
            if observed != source.get("sha256"):
                errors.append(
                    f"research/benchmarks/baselines-v1.json: source hash changed for {source.get('path')}"
                )
    except (OSError, json.JSONDecodeError) as error:
        errors.append(f"research/benchmarks/baselines-v1.json: {error}")
    if (root / "CLAUDE.md").read_text(encoding="utf-8").strip() != "@AGENTS.md":
        errors.append("CLAUDE.md: expected the single canonical @AGENTS.md import")
    if errors:
        for error in errors:
            print(f"ERROR {error}", file=sys.stderr)
        print(f"research validation failed: {len(errors)} error(s)", file=sys.stderr)
        return 1
    print(f"research validation passed: {len(live_record_files(root))} live record(s)")
    return 0


def hash_paths(args: argparse.Namespace, root: Path) -> int:
    for raw in args.paths:
        path = Path(raw)
        if not path.is_absolute():
            path = root / path
        digest = hashlib.sha256()
        with path.open("rb") as source:
            for chunk in iter(lambda: source.read(1024 * 1024), b""):
                digest.update(chunk)
        print(f"{digest.hexdigest()}  {path.relative_to(root) if path.is_relative_to(root) else path}")
    return 0


def commit_lint(args: argparse.Namespace, root: Path) -> int:
    if args.path == "-":
        text = sys.stdin.read()
        label = "stdin"
    else:
        path = Path(args.path)
        if not path.is_absolute():
            path = root / path
        text = path.read_text(encoding="utf-8")
        label = str(path.relative_to(root)) if path.is_relative_to(root) else str(path)
    lines = [line.rstrip() for line in text.splitlines() if line.strip() and not line.lstrip().startswith("#")]
    if not lines:
        print(f"ERROR {label}: empty commit message", file=sys.stderr)
        return 1
    match = re.fullmatch(
        r"(theory|experiment|result|benchmark|infra|fix|docs)\(([a-z0-9][a-z0-9-]*)\): (.{1,72})",
        lines[0],
    )
    if match is None:
        print(
            f"ERROR {label}: subject must be type(lowercase-scope): imperative summary (summary <=72 characters)",
            file=sys.stderr,
        )
        return 1
    commit_type = match.group(1)
    trailers: dict[str, str] = {}
    for line in lines[1:]:
        trailer = re.fullmatch(r"([A-Za-z][A-Za-z0-9-]+):\s*(.+)", line)
        if trailer:
            trailers[trailer.group(1)] = trailer.group(2).strip()
    required = {"Contribution-ID"}
    if commit_type in {"theory", "experiment", "result", "benchmark"}:
        required.update({"Theory-ID", "Evidence-Change"})
    if commit_type in {"experiment", "result", "benchmark"}:
        required.add("Experiment-ID")
    if commit_type in {"result", "benchmark"}:
        required.add("Run-ID")
    if commit_type == "result":
        required.update({"Result-ID", "Result-SHA256"})
    missing = sorted(key for key in required if not trailers.get(key) or trailers[key] == "none")
    errors = [f"{label}: missing non-none trailer {key}" for key in missing]
    patterns = {
        "Theory-ID": r"^TH-[0-9]{8}-[a-z0-9]+(?:-[a-z0-9]+)*-[a-f0-9]{8}$",
        "Experiment-ID": r"^EX-[0-9]{8}-[a-z0-9]+(?:-[a-z0-9]+)*-[a-f0-9]{8}$",
        "Run-ID": r"^RUN-[0-9]{8}T[0-9]{6}Z-[a-f0-9]{8}$",
        "Result-ID": r"^RS-[0-9]{8}T[0-9]{6}Z-[a-f0-9]{8}$",
        "Result-SHA256": r"^[a-f0-9]{64}$",
    }
    for key, pattern in patterns.items():
        value = trailers.get(key)
        if value and value != "none" and re.fullmatch(pattern, value) is None:
            errors.append(f"{label}: invalid {key} {value!r}")
    contribution = trailers.get("Contribution-ID")
    if contribution and contribution != "none":
        for value in [item.strip() for item in contribution.split(",")]:
            if re.fullmatch(r"CT-[0-9]{8}T[0-9]{6}Z-[a-f0-9]{8}", value) is None:
                errors.append(f"{label}: invalid Contribution-ID {value!r}")
    if errors:
        for error in errors:
            print(f"ERROR {error}", file=sys.stderr)
        return 1
    print(f"commit message valid: {commit_type}")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    validation = subparsers.add_parser("validate", help="validate schemas, records, links, and seed leases")
    validation.set_defaults(handler=validate)

    doctor_parser = subparsers.add_parser("doctor", help="print a read-only machine profile")
    doctor_parser.add_argument("--output", help="write a new file (or a file inside an existing directory)")
    doctor_parser.set_defaults(handler=doctor)

    freeze = subparsers.add_parser("freeze", help="freeze and hash a completed draft experiment protocol")
    freeze.add_argument("path")
    freeze.set_defaults(handler=freeze_experiment)

    hashes = subparsers.add_parser("hash", help="print SHA-256 for files")
    hashes.add_argument("paths", nargs="+")
    hashes.set_defaults(handler=hash_paths)

    commit = subparsers.add_parser("commit-lint", help="validate a research commit message and trailers")
    commit.add_argument("path", help="commit-message file, or - for stdin")
    commit.set_defaults(handler=commit_lint)

    new = subparsers.add_parser("new", help="create a collision-resistant draft record")
    new_subparsers = new.add_subparsers(dest="record_type", required=True)

    def add_actor_options(command: argparse.ArgumentParser) -> None:
        command.add_argument("--platform", default="unknown")
        command.add_argument("--model", default="unknown")
        command.add_argument("--agent-id")

    theory = new_subparsers.add_parser("theory")
    theory.add_argument("--slug", required=True)
    theory.add_argument("--title", required=True)
    theory.add_argument("--claim")
    theory.add_argument("--mechanism")
    theory.add_argument("--falsification", action="append", default=[])
    theory.add_argument(
        "--information-class",
        choices=["public-policy", "privileged-teacher", "diagnostic", "engineering", "infrastructure"],
        default="public-policy",
    )
    add_actor_options(theory)
    theory.set_defaults(new_handler=new_theory)

    experiment = new_subparsers.add_parser("experiment")
    experiment.add_argument("--slug", required=True)
    experiment.add_argument("--title", required=True)
    experiment.add_argument("--theory-id", required=True)
    experiment.add_argument("--hypothesis")
    experiment.add_argument("--candidate", required=True)
    experiment.add_argument("--entry-point")
    experiment.add_argument(
        "--classification",
        choices=["engineering", "diagnostic", "algorithmic", "validation", "infrastructure"],
        default="algorithmic",
    )
    experiment.add_argument(
        "--information-boundary",
        choices=["public-policy", "privileged-teacher", "seed-free", "not-applicable"],
        default="public-policy",
    )
    add_actor_options(experiment)
    experiment.set_defaults(new_handler=new_experiment)

    contribution = new_subparsers.add_parser("contribution")
    contribution.add_argument("--summary", required=True)
    contribution.add_argument("--actor-kind", choices=["model", "human", "tool", "mixed"], default="model")
    contribution.add_argument("--level", choices=["L0", "L1", "L2", "L3", "L4"], required=True)
    contribution.add_argument(
        "--role",
        choices=[
            "conceptualization",
            "methodology",
            "software",
            "validation",
            "investigation",
            "formal-analysis",
            "data-curation",
            "compute",
            "visualization",
            "writing",
            "review",
            "orchestration",
        ],
        required=True,
    )
    contribution.add_argument("--degree", choices=["supporting", "substantial", "lead"], required=True)
    contribution.add_argument("--theory-id", action="append", default=[])
    contribution.add_argument("--experiment-id", action="append", default=[])
    contribution.add_argument("--run-id", action="append", default=[])
    contribution.add_argument("--artifact", action="append", default=[])
    contribution.add_argument("--validation", action="append", default=[])
    add_actor_options(contribution)
    contribution.set_defaults(new_handler=new_contribution)
    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    root = repository_root()
    try:
        if args.command == "new":
            path = args.new_handler(args, root)
            print(path.relative_to(root))
            return 0
        return args.handler(args, root)
    except (OSError, ValueError, RuntimeError, json.JSONDecodeError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
