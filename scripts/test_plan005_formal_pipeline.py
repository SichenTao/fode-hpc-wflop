#!/usr/bin/env python3
"""Bounded no-optimization fixture for the Plan-005 formal pipeline."""

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
from pathlib import Path

from plan005_formal_common import (
    build_manifest,
    formal_command,
    result_key,
    result_path,
    validate_manifest,
)


def main() -> int:
    manifest = build_manifest(prepared=True)
    manifest["status"] = "frozen_ready"
    for campaign in manifest["campaigns"]:
        campaign["backend"]["selected_workers"] = 1
        campaign["backend"]["selected_affinity_cpus"] = [19]
        campaign["backend"]["selection_status"] = "accepted_h6"
    validate_manifest(manifest, prepared=False)
    keys: set[str] = set()
    paths: set[str] = set()
    command_routes = 0
    for campaign in manifest["campaigns"]:
        for case in campaign["cases"]:
            seed = campaign["optimization_seeds"][0]
            output = result_path(campaign, case, seed)
            front = (
                output.with_suffix(".front.json")
                if campaign["objective_mode"] == "multiobjective"
                else None
            )
            command = formal_command(
                campaign,
                case,
                seed=seed,
                front_path=front,
            )
            if str(seed) not in command or "1" not in command:
                raise RuntimeError("seed or selected worker route absent")
            if campaign["corpus_id"] == "T46":
                population = int(
                    command[command.index("--population") + 1]
                )
                generations = int(
                    command[command.index("--generations") + 1]
                )
                if (
                    population * (generations + 1)
                    != case["physical_fes_per_run"]
                ):
                    raise RuntimeError("PBEA physical-FES route drift")
            elif (
                command[command.index("--physical-fes") + 1]
                != str(case["physical_fes_per_run"])
            ):
                raise RuntimeError("physical-FES command route drift")
            command_routes += 1
            for seed in campaign["optimization_seeds"]:
                key = json.dumps(
                    result_key(manifest, campaign, case, seed),
                    sort_keys=True,
                )
                path = str(result_path(campaign, case, seed))
                if key in keys or path in paths:
                    raise RuntimeError("formal result identity collision")
                keys.add(key)
                paths.add(path)
    if len(keys) != 28825:
        raise RuntimeError(f"formal result identity drift: {len(keys)}")
    if command_routes != 1153:
        raise RuntimeError(f"formal command route drift: {command_routes}")
    with tempfile.TemporaryDirectory(
        prefix="plan005-formal-pipeline-"
    ) as directory:
        manifest_path = Path(directory) / "final-like.json"
        manifest_path.write_text(
            json.dumps(manifest, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        ready_pair = next(
            campaign["pair_id"]
            for campaign in manifest["campaigns"]
            if campaign["execution_admission"] == "ready_cpu"
        )
        completed = subprocess.run(
            [
                sys.executable,
                "scripts/run_plan005_formal_campaigns.py",
                "--all-admissible",
                "--backend-parallelism",
                "1",
                "--manifest",
                str(manifest_path),
                "--only-pair",
                ready_pair,
                "--max-runs",
                "1",
                "--dry-run",
            ],
            check=True,
            capture_output=True,
            text=True,
        )
        receipt = json.loads(completed.stdout)
        if receipt["pending_count"] != 1:
            raise RuntimeError("dry-run pending cardinality drift")
        subprocess.run(
            [
                sys.executable,
                "scripts/audit_plan005_training_resume.py",
                "--strict",
                "--manifest",
                str(manifest_path),
            ],
            check=True,
            capture_output=True,
            text=True,
        )
    print(
        "plan005_formal_pipeline_fixture_pass "
        "targets=23 cases=1153 command_routes=1153 "
        "result_keys=28825 dry_run_only=1 "
        "optimizer_processes_started=0"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
