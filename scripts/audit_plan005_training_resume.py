#!/usr/bin/env python3
"""Audit learning-artifact boundaries and resumability for Plan 005.

This audit deliberately distinguishes bounded H5/H6 artifacts from
paper-scale training artifacts.  It never upgrades a bounded reconstruction
to a completed paper-scale training claim.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from plan005_formal_common import (
    FINAL_MANIFEST,
    ROOT,
    require,
    sha256,
    validate_manifest,
)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--strict", action="store_true", required=True)
    parser.add_argument("--manifest", type=Path, default=FINAL_MANIFEST)
    arguments = parser.parse_args()
    path = arguments.manifest.resolve()
    require(path.is_file(), f"manifest absent: {path}")
    manifest = json.loads(path.read_text(encoding="utf-8"))
    validate_manifest(manifest, prepared=False)
    deferred = [
        campaign
        for campaign in manifest["campaigns"]
        if campaign["execution_admission"]
        == "validated_deferred_full_training"
    ]
    ready = [
        campaign
        for campaign in manifest["campaigns"]
        if campaign["execution_admission"] == "ready_cpu"
    ]
    require(
        {campaign["corpus_id"] for campaign in deferred}
        == {"Y36", "T42", "T45"},
        "learning-resource boundary drift",
    )
    for campaign in deferred:
        admission = campaign["training_admission"]
        require(
            admission["namespaces_disjoint"] is True
            and admission["formal_training_seed_namespace"]
            != admission["optimization_seed_namespace"],
            f"{campaign['pair_id']}: training/optimization seed leakage",
        )
        artifact = admission["bounded_h6_artifact"]
        artifact_path = ROOT / artifact["artifact_logical_path"]
        require(
            artifact_path.is_file()
            and sha256(artifact_path) == artifact["artifact_sha256"]
            and artifact["source_commit"] == manifest["source_commit"],
            f"{campaign['pair_id']}: bounded H6 artifact drift",
        )
        require(
            "paper-scale" in admission["reason"]
            and len(admission["resume_requires"]) == 5,
            f"{campaign['pair_id']}: full-training claim boundary absent",
        )
    fqfode = [
        campaign
        for campaign in ready
        if campaign["corpus_id"] == "S04"
    ]
    require(len(fqfode) == 1, "FQFODE shared offline model route absent")
    model_root = ROOT / "shared/models/fqfode_seeded"
    require(
        model_root.is_dir() and any(model_root.iterdir()),
        "FQFODE shared offline model directory is absent",
    )
    print(
        "plan005_training_resume_audit_pass "
        f"ready_cpu_targets={len(ready)} "
        f"bounded_learning_targets={len(deferred)} "
        "paper_scale_training_completed=0 "
        "bounded_artifacts_not_promoted=1 seed_namespaces_disjoint=1"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
