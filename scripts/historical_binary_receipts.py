"""Portable validation for retired historical build artifacts."""

from __future__ import annotations

import hashlib
import json
import re
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MIGRATION = (
    ROOT
    / "shared/contracts/"
    "plan005_historical_step11_receipt_migration.json"
)
SHA256 = re.compile(r"^[0-9a-f]{64}$")


def verify_historical_binary(
    binary_target: str,
    binary_path: str,
    binary_sha256: str,
) -> str:
    """Return ``present`` or ``retired`` after fail-closed validation."""
    if not SHA256.fullmatch(binary_sha256):
        raise RuntimeError(
            f"{binary_target}: historical binary SHA-256 is invalid"
        )
    path = ROOT / binary_path
    if path.is_file():
        observed = hashlib.sha256(path.read_bytes()).hexdigest()
        if observed != binary_sha256:
            raise RuntimeError(
                f"{binary_target}: historical binary hash changed"
            )
        return "present"

    migration = json.loads(MIGRATION.read_text(encoding="utf-8"))
    if (
        migration["schema_version"] != 1
        or migration["historical_source_commit"]
        != "f4cf564e60cfc2fbba804c49d0866f3aff82de59"
        or migration["legacy_build_root"] != "build/step10-release"
        or migration["disposition"]
        != "retired_build_artifact_unavailable"
        or "no current binary is substituted" not in migration["policy"]
        or "independently path- and SHA-256-audited"
        not in migration["raw_evidence_policy"]
    ):
        raise RuntimeError("historical binary migration contract drifted")
    entries = [
        row
        for row in migration["binaries"]
        if row
        == {
            "binary_target": binary_target,
            "binary_path": binary_path,
            "binary_sha256": binary_sha256,
        }
    ]
    if len(entries) != 1:
        raise RuntimeError(
            f"{binary_target}: absent binary lacks exact migration entry"
        )
    return "retired"


def verify_historical_source(
    source_path: str,
    source_sha256: str,
    source_commit: str,
) -> str:
    """Validate current source or its exact historical Git object."""
    if not SHA256.fullmatch(source_sha256):
        raise RuntimeError(
            f"{source_path}: historical source SHA-256 is invalid"
        )
    current = ROOT / source_path
    if (
        current.is_file()
        and hashlib.sha256(current.read_bytes()).hexdigest()
        == source_sha256
    ):
        return "current"
    migration = json.loads(MIGRATION.read_text(encoding="utf-8"))
    if migration["historical_source_commit"] != source_commit:
        raise RuntimeError(f"{source_path}: historical source commit differs")
    entries = [
        row
        for row in migration["historical_oracle_sources"]
        if row == {"path": source_path, "sha256": source_sha256}
    ]
    if len(entries) != 1:
        raise RuntimeError(
            f"{source_path}: changed source lacks exact migration entry"
        )
    completed = subprocess.run(
        ["git", "show", f"{source_commit}:{source_path}"],
        cwd=ROOT,
        capture_output=True,
        check=False,
    )
    if (
        completed.returncode != 0
        or hashlib.sha256(completed.stdout).hexdigest() != source_sha256
    ):
        raise RuntimeError(
            f"{source_path}: historical Git source object differs"
        )
    return "historical_git_object"
