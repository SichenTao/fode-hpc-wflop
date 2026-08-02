#!/usr/bin/env python3
"""Audit the excluded H6 campaign and deterministic learning-lane repair."""

from __future__ import annotations

import hashlib
import json
import math
import subprocess
from pathlib import Path
from typing import Any

from run_hpc_core_target_scaling import canonical_sha256, scientific_payload


ROOT = Path(__file__).resolve().parents[1]
EXCLUSION = (
    ROOT
    / "evidence/performance/excluded/"
    "plan005_h6_pre_deterministic_lane_exclusion_20260730.json"
)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def path(value: str) -> Path:
    result = Path(value)
    return result if result.is_absolute() else ROOT / result


def sha256(file: Path) -> str:
    return hashlib.sha256(file.read_bytes()).hexdigest()


def load_json(file: Path) -> dict[str, Any]:
    return json.loads(file.read_text(encoding="utf-8"))


def main() -> int:
    require(EXCLUSION.is_file(), "deterministic-lane exclusion absent")
    receipt = load_json(EXCLUSION)
    require(
        receipt["schema_version"] == 1
        and receipt["status"] == "excluded_and_partially_reusable",
        "deterministic-lane exclusion schema or status drift",
    )
    excluded = receipt["excluded_campaign"]
    for field in ("raw_observations", "environment_sidecar", "stdout", "stderr"):
        item = excluded[field]
        file = path(item["logical_path"])
        require(
            file.is_file() and sha256(file) == item["sha256"],
            f"{field}: excluded campaign file absent or changed",
        )
    raw_lines = [
        json.loads(line)
        for line in path(
            excluded["raw_observations"]["logical_path"]
        ).read_text(encoding="utf-8").splitlines()
        if line.strip()
    ]
    require(
        len(raw_lines) == excluded["raw_observations"]["record_count"] == 2
        and raw_lines[0]["record_type"] == "campaign_header"
        and raw_lines[1]["record_type"] == "observation",
        "excluded campaign record coverage drift",
    )
    candidates = receipt["diagnostic_candidates"]
    loaded_candidates = {}
    for name, item in candidates.items():
        file = path(item["logical_path"])
        stderr = path(item["stderr_logical_path"])
        require(
            file.is_file()
            and sha256(file) == item["sha256"]
            and stderr.is_file()
            and sha256(stderr) == item["stderr_sha256"],
            f"{name}: diagnostic candidate absent or changed",
        )
        loaded_candidates[name] = load_json(file)
    worker_1 = raw_lines[1]["raw_result"]
    invalid = loaded_candidates["worker_dependent_torch_reduction"]
    repaired = loaded_candidates["deterministic_learning_lane"]
    exact_fields = (
        "front",
        "front_hash",
        "learning_decision_hash",
        "population_layout_hash",
        "model_hash",
        "numerical_state",
        "physical_fes",
        "generations",
    )
    require(
        all(worker_1[field] == repaired[field] for field in exact_fields)
        and canonical_sha256(scientific_payload(worker_1))
        == canonical_sha256(scientific_payload(repaired)),
        "deterministic learning lane did not preserve exact science",
    )
    require(
        any(worker_1[field] != invalid[field] for field in exact_fields)
        and invalid["thread_topology"]["torch_intraop_threads"] == 2
        and repaired["thread_topology"]["torch_intraop_threads"] == 1,
        "worker-dependent LibTorch reduction failure is no longer present",
    )
    validation = receipt["cross_worker_validation"]
    require(
        math.isclose(
            validation["end_to_end_speedup"],
            worker_1["total_wall_seconds"] / repaired["total_wall_seconds"],
            rel_tol=1.0e-15,
            abs_tol=0.0,
        )
        and math.isclose(
            validation["evaluator_speedup"],
            worker_1["evaluator_wall_seconds"]
            / repaired["evaluator_wall_seconds"],
            rel_tol=1.0e-15,
            abs_tol=0.0,
        ),
        "deterministic-lane speedup arithmetic drift",
    )
    h5 = subprocess.run(
        [
            "python3",
            "scripts/audit_plan005_h5_learning_phase_revalidation.py",
        ],
        cwd=ROOT,
        capture_output=True,
        text=True,
    )
    require(
        h5.returncode == 0
        and "taae_model=exact" in h5.stdout
        and "alga_rlpso_state=exact" in h5.stdout,
        "deterministic-lane H5 receipt failed independent audit",
    )
    print(
        "plan005_h6_deterministic_lane_repair_audit_pass "
        "h5=accepted long_horizon_science=exact "
        f"end_to_end_speedup={validation['end_to_end_speedup']:.6f} "
        f"evaluator_speedup={validation['evaluator_speedup']:.6f}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
