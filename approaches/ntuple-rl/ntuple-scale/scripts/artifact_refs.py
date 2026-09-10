#!/usr/bin/env python3
"""Resolve the public archive reference a finalized result record must cite.

A finalized result record cites its per-game artifact by the immutable public
reference ``https://data.drop7.dev/runs/<run-id>/<key>#sha256=<digest>``
(AGENTS.md, "Remote research artifacts"), never by a path into the ignored
``runs/`` directory.  This module finds that reference for a local run
artifact from what the run record and the publisher already know, and refuses
to invent one: a reference is returned only when its digest equals the digest
of the local file.

Sources, in order: an explicit reference from the command line, the run
record's ``artifactRefs`` (``research/runs/<run-id>.json``), then the
publisher's manifest (``runs/<run-id>/published.jsonl`` or
``runs/<run-id>/ntuple-scale/published.jsonl``; only lines whose status is
``uploaded`` or ``already-published`` count, a dry-run preview is never a
publication).
"""
from __future__ import annotations

import hashlib
import json
import os
import re
from urllib.parse import urlsplit

PUBLIC_ORIGIN = "https://data.drop7.dev"
PUBLISHED_STATUSES = frozenset({"uploaded", "already-published"})


class UnresolvedArtifact(Exception):
    """No verified public reference exists for the local artifact."""


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def parse_public_ref(ref, run_id):
    """Return (object key, digest) for a well-formed public reference below run_id, else None."""
    if not isinstance(ref, str):
        return None
    parsed = urlsplit(ref)
    if parsed.scheme != "https" or parsed.netloc != "data.drop7.dev" or parsed.query:
        return None
    prefix = f"/runs/{run_id}/"
    if not parsed.path.startswith(prefix) or parsed.path == prefix:
        return None
    match = re.fullmatch(r"sha256=([a-f0-9]{64})", parsed.fragment)
    if match is None:
        return None
    return parsed.path[1:], match.group(1)


def published_refs(root, run_id):
    """Yield every public reference recorded for run_id, from the run record and the publisher manifest."""
    run_record = os.path.join(root, "research", "runs", f"{run_id}.json")
    if os.path.isfile(run_record):
        with open(run_record, encoding="utf-8") as handle:
            record = json.load(handle)
        for ref in record.get("artifactRefs", []):
            if isinstance(ref, str) and ref.startswith(PUBLIC_ORIGIN):
                yield ref
    for manifest in (
        os.path.join(root, "runs", run_id, "published.jsonl"),
        os.path.join(root, "runs", run_id, "ntuple-scale", "published.jsonl"),
    ):
        if not os.path.isfile(manifest):
            continue
        with open(manifest, encoding="utf-8") as handle:
            for line in handle:
                line = line.strip()
                if not line:
                    continue
                try:
                    entry = json.loads(line)
                except ValueError:
                    continue
                if entry.get("status") in PUBLISHED_STATUSES and isinstance(entry.get("ref"), str):
                    yield entry["ref"]


def resolve_public_ref(root, run_id, local_path, digest, explicit=None):
    """Return the public reference of the run artifact at local_path (relative to root).

    ``digest`` is the SHA-256 of the local file; every returned reference carries
    it in its fragment.  ``explicit`` is a reference named on the command line
    and is checked, not trusted.  A reference whose object key equals
    local_path is preferred; otherwise a single digest match is accepted.
    """
    if explicit is not None:
        parsed = parse_public_ref(explicit, run_id)
        if parsed is None:
            raise UnresolvedArtifact(
                f"{explicit} is not a public reference of {run_id} "
                f"({PUBLIC_ORIGIN}/runs/{run_id}/<key>#sha256=<digest>)"
            )
        if parsed[1] != digest:
            raise UnresolvedArtifact(f"{explicit} carries digest {parsed[1]} but {local_path} hashes to {digest}")
        return explicit
    by_key, by_digest, seen = [], [], set()
    for ref in published_refs(root, run_id):
        parsed = parse_public_ref(ref, run_id)
        if parsed is None or parsed[1] != digest or ref in seen:
            continue
        seen.add(ref)
        (by_key if parsed[0] == local_path else by_digest).append(ref)
    if by_key:
        return by_key[0]
    if len(by_digest) == 1:
        return by_digest[0]
    if by_digest:
        raise UnresolvedArtifact(
            f"{len(by_digest)} published objects of {run_id} carry the digest of {local_path} under other keys; "
            "pass --per-game-ref to choose one: " + ", ".join(by_digest)
        )
    raise UnresolvedArtifact(
        f"no published object of {run_id} carries the digest of {local_path} ({digest}); publish it first "
        f"(npm run artifact:publish -- --run-id {run_id} --file {local_path} --public), cite the printed "
        f"reference in research/runs/{run_id}.json, or pass it as --per-game-ref"
    )


def citation_limitation(per_game_path, local_path):
    """The limitation sentence a record carries about how its per-game artifact is cited."""
    if per_game_path == local_path:
        return (
            f"DRAFT: the per-game artifact is cited by its local path {local_path}, which exists only on the "
            "workstation; the record is not finalized until the artifact is published and the citation is "
            "its public archive reference."
        )
    return (
        "The per-game artifact is cited by its public archive reference (data.drop7.dev, immutable run-scoped "
        f"key, digest in the fragment); a byte-identical copy remains under {local_path} on the workstation "
        "and a promoted copy under artifacts/results/."
    )


def artifact_manifest_ref(root, experiment_id, run_id):
    """Return the repository path of the promoted-artifact manifest when it exists, else None."""
    relative = os.path.join("artifacts", "results", experiment_id, run_id, "manifest.json")
    return relative if os.path.isfile(os.path.join(root, relative)) else None
