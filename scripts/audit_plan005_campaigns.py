#!/usr/bin/env python3
"""Read-only fail-closed audit of Plan-005 formal optimization results."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any

from plan005_formal_common import (
    FINAL_MANIFEST,
    ROOT,
    formal_command,
    require,
    result_key,
    result_path,
    sha256,
    validate_manifest,
)


def audit_result(
    manifest: dict[str, Any],
    campaign: dict[str, Any],
    case: dict[str, Any],
    seed: int,
) -> None:
    path = result_path(campaign, case, seed)
    require(path.is_file(), f"formal result absent: {path}")
    document = json.loads(path.read_text(encoding="utf-8"))
    expected_key = result_key(manifest, campaign, case, seed)
    require(
        document.get("schema_version") == 1
        and document.get("status") == "validated_complete",
        f"result is not validated complete: {path}",
    )
    require(
        document.get("result_key") == expected_key,
        f"result key drift: {path}",
    )
    require(
        document.get("observed_physical_fes")
        == case["physical_fes_per_run"],
        f"physical FES drift: {path}",
    )
    binary = ROOT / campaign["backend"]["binary_logical_path"]
    require(
        binary.is_file()
        and sha256(binary) == campaign["backend"]["binary_sha256"]
        and document.get("binary_sha256")
        == campaign["backend"]["binary_sha256"],
        f"binary identity drift: {path}",
    )
    front_path = (
        path.with_suffix(".front.json")
        if campaign["corpus_id"] == "T46"
        else None
    )
    expected_command = formal_command(
        campaign,
        case,
        seed=seed,
        front_path=front_path,
    )
    require(
        document.get("command") == expected_command,
        f"formal command drift: {path}",
    )
    front = document.get("front_artifact")
    if front is not None:
        artifact = ROOT / front["logical_path"]
        require(
            artifact.is_file()
            and sha256(artifact) == front["sha256"]
            and artifact.stat().st_size == front["bytes"],
            f"front artifact drift: {artifact}",
        )
    if campaign["corpus_id"] == "T46":
        require(front is not None, f"PBEA front receipt absent: {path}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--all-admissible", action="store_true", required=True)
    parser.add_argument("--seeds", type=int, choices=(25,), required=True)
    parser.add_argument("--strict", action="store_true", required=True)
    parser.add_argument("--manifest", type=Path, default=FINAL_MANIFEST)
    arguments = parser.parse_args()
    path = arguments.manifest.resolve()
    require(path.is_file(), f"manifest absent: {path}")
    manifest = json.loads(path.read_text(encoding="utf-8"))
    validate_manifest(manifest, prepared=False)
    campaigns = [
        campaign
        for campaign in manifest["campaigns"]
        if campaign["execution_admission"] == "ready_cpu"
    ]
    expected_runs = sum(
        campaign["case_count"] * 25 for campaign in campaigns
    )
    observed_runs = 0
    for campaign in campaigns:
        for case in campaign["cases"]:
            for seed in campaign["optimization_seeds"]:
                audit_result(manifest, campaign, case, seed)
                observed_runs += 1
    require(
        observed_runs == expected_runs,
        "formal result cardinality drift",
    )
    print(
        "plan005_formal_campaign_audit_pass "
        f"campaigns={len(campaigns)} runs={observed_runs} "
        "backend_parallelism=1 exact_physical_fes=1 "
        "result_keys=complete"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
