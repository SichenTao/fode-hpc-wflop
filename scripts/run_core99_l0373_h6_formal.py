#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: resumable L0373 Waffle H6 and paper-native campaign
Paper DOI: 10.1016/j.renene.2021.10.032
Protocol: complete declared N16-W36 one/all-core H6 followed by all six
paper-native profiles on every available Waffle core.
Public sources, missing information, conflicts, corrections, reconstruction,
semantic IDs, backend, controlling contract and claim boundary:
hpc/core99_cpp/include/core99/chen_l0373.hpp
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time
from typing import Any


METHOD = "l0373_pso_warm_dbhm_projected_declared_v1"
PROBLEM = "l0373_joint_layout_yaw_induction_floris_declared_v1"
PROTOCOL = "l0373_native_illustrative_16_36_360_80_12_180_v1"
PROFILES = (
    "illustrative-unrestricted", "illustrative-4d", "n16-w36",
    "n16-w360", "n80-w12", "n80-w180",
)
H6_PROFILE = "n16-w36"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while block := stream.read(1024 * 1024):
            digest.update(block)
    return digest.hexdigest()


def write_json(path: Path, payload: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(
        json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    os.replace(temporary, path)


def call_json(command: list[str]) -> dict | list:
    completed = subprocess.run(
        command, check=True, capture_output=True, text=True, timeout=14_400.0
    )
    return json.loads(completed.stdout)


def optimize(
    *, binary: Path, output: Path, profile: str, seed: int, workers: int,
    pso_trials: int, pso_population: int, pso_iterations: int,
    control_passes: int, dbhm_iterations: int, source_commit: str,
) -> dict[str, Any]:
    identity = {
        "source_commit": source_commit,
        "profile": profile,
        "seed": seed,
        "requested_workers": workers,
        "pso_trials": pso_trials,
        "pso_population": pso_population,
        "pso_iterations": pso_iterations,
        "control_passes": control_passes,
        "dbhm_iterations_requested": dbhm_iterations,
    }
    if output.exists():
        previous = json.loads(output.read_text(encoding="utf-8"))
        if all(previous.get(key) == value for key, value in identity.items()):
            return previous
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix(".binary.tmp")
    started = time.monotonic()
    subprocess.run([
        str(binary), "--profile", profile, "--seed", str(seed),
        "--workers", str(workers), "--pso-trials", str(pso_trials),
        "--pso-population", str(pso_population),
        "--pso-iterations", str(pso_iterations),
        "--control-passes", str(control_passes),
        "--dbhm-iterations", str(dbhm_iterations),
        "--output", str(temporary),
    ], check=True, timeout=14_400.0)
    payload = json.loads(temporary.read_text(encoding="utf-8"))
    temporary.unlink()
    payload.update({
        "source_commit": source_commit,
        "profile": profile,
        "binary_sha256": sha256(binary),
        "runner_wall_seconds": time.monotonic() - started,
        "control_passes": control_passes,
        "dbhm_iterations_requested": dbhm_iterations,
    })
    write_json(output, payload)
    return payload


def validate(payload: dict[str, Any], profile: str, workers: int) -> None:
    require(payload["profile"] == profile, "profile")
    require(payload["profile_id"].startswith("L0373_"), "binary profile id")
    require(payload["method_semantic_id"] == METHOD, "method")
    require(payload["problem_semantic_id"] == PROBLEM, "problem")
    require(payload["protocol_semantic_id"] == PROTOCOL, "protocol")
    require(payload["requested_workers"] == workers, "workers")
    roles = payload["cases"]
    expected_count = 3 if profile.startswith("illustrative-") else 5
    require(len(roles) == expected_count, "paper role count")
    for role in roles:
        evaluation = role["evaluation"]
        require(evaluation["feasible"] is True, "feasibility")
        require(evaluation["aep_gwh"] > 0.0, "AEP")
        require(0.0 < evaluation["efficiency_percent"] <= 100.0 + 1e-10,
                "efficiency")
    if expected_count == 5:
        values = [role["evaluation"]["aep_gwh"] for role in roles]
        require(values[1] >= values[0], "control improvement")
        require(values[3] >= values[2], "sequential improvement")
        require(values[4] >= values[3], "joint incumbent")
        if workers > 1:
            require(payload["observed_workers"] == workers,
                    "all requested workers participated")


def run_h6(arguments: argparse.Namespace, root: Path) -> dict[str, Any]:
    pair = {}
    for workers in (1, arguments.total_workers):
        payload = optimize(
            binary=arguments.binary,
            output=root / "h6" / f"{H6_PROFILE}-w{workers:02d}.json",
            profile=H6_PROFILE, seed=37301, workers=workers,
            pso_trials=arguments.pso_trials,
            pso_population=arguments.pso_population,
            pso_iterations=arguments.pso_iterations,
            control_passes=arguments.control_passes,
            dbhm_iterations=arguments.dbhm_iterations,
            source_commit=arguments.source_commit,
        )
        validate(payload, H6_PROFILE, workers)
        pair[workers] = payload
    serial = pair[1]
    parallel = pair[arguments.total_workers]
    for key in (
        "scientific_hash", "complete_layout_evaluations",
        "single_wind_state_evaluations", "dbhm_iterations_completed",
        "final_consensus_violation_m", "cases",
    ):
        require(serial[key] == parallel[key], f"H6 identity: {key}")
    speedup = {
        "end_to_end": serial["end_to_end_seconds"]
            / parallel["end_to_end_seconds"],
        "isolated_layout": serial["isolated_layout_stage_seconds"]
            / parallel["isolated_layout_stage_seconds"],
        "control": serial["control_stage_seconds"]
            / parallel["control_stage_seconds"],
        "dbhm": serial["dbhm_stage_seconds"]
            / parallel["dbhm_stage_seconds"],
    }
    require(speedup["end_to_end"] > 1.0, "end-to-end speedup")
    summary = {
        "status": "pass", "source_commit": arguments.source_commit,
        "profile": H6_PROFILE, "one_worker": serial,
        "all_worker": parallel, "speedup": speedup,
        "same_cases_controls_layouts_work_and_hash": True,
    }
    write_json(root / "h6" / "summary.json", summary)
    return summary


def run_formal(arguments: argparse.Namespace, root: Path) -> dict[str, Any]:
    rows = []
    for index, profile in enumerate(PROFILES):
        payload = optimize(
            binary=arguments.binary,
            output=root / "formal" / f"{profile}.json",
            profile=profile, seed=3730100 + index,
            workers=arguments.total_workers,
            pso_trials=arguments.pso_trials,
            pso_population=arguments.pso_population,
            pso_iterations=arguments.pso_iterations,
            control_passes=arguments.control_passes,
            dbhm_iterations=arguments.dbhm_iterations,
            source_commit=arguments.source_commit,
        )
        validate(payload, profile, arguments.total_workers)
        rows.append({
            "profile": profile,
            "seed": payload["seed"],
            "turbines": payload["metadata"]["turbines"],
            "wind_scenarios": payload["metadata"]["wind_scenarios"],
            "paper_aep_anchors_gwh": payload["metadata"]["paper_aep_anchors_gwh"],
            "role_aep_gwh": [
                role["evaluation"]["aep_gwh"] for role in payload["cases"]
            ],
            "complete_layout_evaluations": payload["complete_layout_evaluations"],
            "single_wind_state_evaluations":
                payload["single_wind_state_evaluations"],
            "dbhm_iterations_completed": payload["dbhm_iterations_completed"],
            "final_consensus_violation_m": payload["final_consensus_violation_m"],
            "end_to_end_seconds": payload["end_to_end_seconds"],
            "scientific_hash": payload["scientific_hash"],
        })
        write_json(root / "formal" / "summary.partial.json", {
            "status": "running", "source_commit": arguments.source_commit,
            "completed_profiles": len(rows), "expected_profiles": len(PROFILES),
            "rows": rows,
        })
    summary = {
        "status": "pass", "source_commit": arguments.source_commit,
        "paper_native_profiles": len(PROFILES),
        "random_seed_repeats_per_profile": 1,
        "workers_per_profile": arguments.total_workers,
        "declared_parameters": {
            "pso_trials": arguments.pso_trials,
            "pso_population": arguments.pso_population,
            "pso_iterations": arguments.pso_iterations,
            "control_passes": arguments.control_passes,
            "dbhm_iterations": arguments.dbhm_iterations,
        },
        "rows": rows,
        "claim_boundary": (
            "source-backed flexible academic reconstruction; not author target "
            "code, private arrays, MATLAB/FLORISSE trajectory or numerical replay"
        ),
    }
    write_json(root / "formal" / "summary.json", summary)
    return summary


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--output-root", required=True, type=Path)
    parser.add_argument("--source-commit", required=True)
    parser.add_argument("--total-workers", required=True, type=int)
    parser.add_argument("--pso-trials", type=int, default=10)
    parser.add_argument("--pso-population", type=int, default=40)
    parser.add_argument("--pso-iterations", type=int, default=30)
    parser.add_argument("--control-passes", type=int, default=5)
    parser.add_argument("--dbhm-iterations", type=int, default=12)
    parser.add_argument("--phase", choices=("all", "h6", "formal"), default="all")
    arguments = parser.parse_args()
    arguments.binary = arguments.binary.resolve()
    root = arguments.output_root.resolve()
    root.mkdir(parents=True, exist_ok=True)
    h6 = run_h6(arguments, root) if arguments.phase in ("all", "h6") else None
    formal = run_formal(arguments, root) if arguments.phase in ("all", "formal") else None
    if arguments.phase == "all":
        assert h6 is not None and formal is not None
        write_json(root / "campaign_summary.json", {
            "status": "pass", "h6_status": h6["status"],
            "formal_profiles": len(formal["rows"]),
            "output_root": str(root),
        })
    print(json.dumps({
        "status": "pass", "phase": arguments.phase,
        "h6_speedup": None if h6 is None else h6["speedup"]["end_to_end"],
        "formal_profiles": None if formal is None else len(formal["rows"]),
        "output_root": str(root),
    }, sort_keys=True))


if __name__ == "__main__":
    main()
