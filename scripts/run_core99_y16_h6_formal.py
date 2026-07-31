#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: resumable Y16 Waffle H6 and paper-native campaign
Paper DOI: 10.1109/TSTE.2026.3686029
First-party supporting patent: CN121683298A/CN121683298B
Protocol: 20 identical angle-pattern tasks for one/all-core H6; then every
one of the 31 deterministic paper-native target roles at 18 angles, ten
patterns, the declared 10000-second per-subproblem ceiling and all cores.
Public asset, missing information, conflicts, corrections, reconstruction,
semantic IDs, production backend, controlling contract and claim boundary:
hpc/core99_cpp/include/core99/huang_y16.hpp
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


METHOD = "y16_imm_bmm_bounded_dinkelbach_highs_reconstruction_v1"
PROBLEM = "y16_regular_seabed_ti_lcoe_figure_proxy_v1"
PROTOCOL = "y16_native_31role_deterministic_v1"
H6_CASE = "Y16_case1_type3_n40_g2p5_imm_ac_i3"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while block := stream.read(1024*1024):
            digest.update(block)
    return digest.hexdigest()


def write_json(path: Path, payload: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix+".tmp")
    temporary.write_text(
        json.dumps(payload, indent=2, sort_keys=True)+"\n", encoding="utf-8"
    )
    os.replace(temporary, path)


def list_cases(binary: Path) -> list[dict[str, Any]]:
    completed = subprocess.run(
        [str(binary), "--action", "list-cases"], check=True,
        capture_output=True, text=True,
    )
    payload = json.loads(completed.stdout)
    require(isinstance(payload, list) and len(payload) == 31, "31-role matrix")
    return payload


def execute(
    *, binary: Path, output: Path, case: str, workers: int,
    angle_start: int, angle_count: int, pattern_start: int, pattern_count: int,
    maximum_bda_iterations: int, mip_time_limit_seconds: float,
    source_commit: str, timeout_seconds: float,
) -> dict[str, Any]:
    expected = {
        "source_commit": source_commit,
        "case_id": case,
        "requested_workers": workers,
        "configured_angle_start": angle_start,
        "configured_angle_count": angle_count,
        "configured_pattern_start": pattern_start,
        "configured_pattern_count": pattern_count,
        "configured_maximum_bda_iterations": maximum_bda_iterations,
        "configured_mip_time_limit_seconds": mip_time_limit_seconds,
    }
    if output.exists():
        previous = json.loads(output.read_text(encoding="utf-8"))
        if all(previous.get(key) == value for key, value in expected.items()):
            return previous
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix(".binary.tmp")
    command = [
        str(binary), "--case", case, "--workers", str(workers),
        "--angle-start", str(angle_start), "--angle-count", str(angle_count),
        "--pattern-start", str(pattern_start),
        "--pattern-count", str(pattern_count),
        "--maximum-bda-iterations", str(maximum_bda_iterations),
        "--mip-time-limit-seconds", str(mip_time_limit_seconds),
        "--output", str(temporary),
    ]
    started = time.monotonic()
    completed = subprocess.run(
        command, text=True, capture_output=True, timeout=timeout_seconds
    )
    if completed.returncode:
        raise RuntimeError(completed.stderr or completed.stdout)
    payload = json.loads(temporary.read_text(encoding="utf-8"))
    temporary.unlink()
    payload.update({
        **expected,
        "binary_sha256": sha256(binary),
        "runner_wall_seconds": time.monotonic()-started,
    })
    write_json(output, payload)
    return payload


def validate(
    payload: dict[str, Any], *, case: str, workers: int,
    expected_tasks: int, paper_expected_infeasible: bool,
) -> None:
    require(payload["metadata"]["case_id"] == case, "case")
    require(payload["method_semantic_id"] == METHOD, "method")
    require(payload["problem_semantic_id"] == PROBLEM, "problem")
    require(payload["protocol_semantic_id"] == PROTOCOL, "protocol")
    require(payload["requested_workers"] == workers, "workers")
    require(payload["generated_subproblems"] == expected_tasks, "task matrix")
    require(payload["evaluator_rejected_subproblems"] == 0, "evaluator rejection")
    if payload["layout"]:
        require(payload["evaluation"]["feasible"] is True, "feasible layout")
        require(
            len(payload["layout"]) == payload["metadata"]["turbine_count"],
            "exact turbine count",
        )
        minimum = 5.0*payload["metadata"]["rotor_diameter_m"]
        require(
            payload["evaluation"]["minimum_spacing_m"] >= minimum-1e-5,
            "5D spacing",
        )
    else:
        require(paper_expected_infeasible, "unexpected missing incumbent")
    if workers > 1 and expected_tasks > 1:
        require(payload["observed_workers"] >= 2, "worker participation")


def run_h6(arguments: argparse.Namespace, root: Path) -> dict[str, Any]:
    pair = {}
    for workers in (1, arguments.total_workers):
        payload = execute(
            binary=arguments.binary,
            output=root/"h6"/f"{H6_CASE}-w{workers:02d}.json",
            case=H6_CASE, workers=workers,
            angle_start=1, angle_count=4, pattern_start=0, pattern_count=5,
            maximum_bda_iterations=2,
            mip_time_limit_seconds=arguments.h6_mip_time_limit_seconds,
            source_commit=arguments.source_commit,
            timeout_seconds=max(7200.0, 40.0*arguments.h6_mip_time_limit_seconds),
        )
        validate(
            payload, case=H6_CASE, workers=workers, expected_tasks=20,
            paper_expected_infeasible=False,
        )
        pair[workers] = payload
    serial, parallel = pair[1], pair[arguments.total_workers]
    for key in (
        "scientific_hash", "selected_angle_degrees", "selected_pattern",
        "layout", "evaluation",
    ):
        require(serial[key] == parallel[key], f"H6 scientific identity: {key}")
    speedup = {
        "end_to_end": serial["end_to_end_seconds"]
            / parallel["end_to_end_seconds"],
        "coefficient_aggregate_to_wall_parallelism":
            parallel["coefficient_seconds"]/parallel["end_to_end_seconds"],
        "mip_aggregate_to_wall_parallelism":
            parallel["mip_seconds"]/parallel["end_to_end_seconds"],
    }
    require(speedup["end_to_end"] > 1.0, "H6 end-to-end speedup")
    summary = {
        "status": "pass", "source_commit": arguments.source_commit,
        "case_id": H6_CASE, "identical_angle_pattern_tasks": 20,
        "one_worker": serial, "all_worker": parallel,
        "same_layout_objective_physics_and_hash": True,
        "speedup": speedup,
    }
    write_json(root/"h6"/"summary.json", summary)
    return summary


def run_formal(
    arguments: argparse.Namespace, root: Path,
    cases: list[dict[str, Any]],
) -> dict[str, Any]:
    rows = []
    for metadata in cases:
        case = metadata["case_id"]
        payload = execute(
            binary=arguments.binary,
            output=root/"formal"/f"{case}.json",
            case=case, workers=arguments.total_workers,
            angle_start=0, angle_count=18, pattern_start=0, pattern_count=10,
            maximum_bda_iterations=arguments.maximum_bda_iterations,
            mip_time_limit_seconds=arguments.formal_mip_time_limit_seconds,
            source_commit=arguments.source_commit,
            timeout_seconds=max(
                86400.0,
                120.0*arguments.formal_mip_time_limit_seconds,
            ),
        )
        validate(
            payload, case=case, workers=arguments.total_workers,
            expected_tasks=180,
            paper_expected_infeasible=metadata["expected_paper_infeasible"],
        )
        rows.append({
            "case_id": case,
            "paper_table_role": metadata["paper_table_role"],
            "paper_expected_infeasible": metadata["expected_paper_infeasible"],
            "status": payload["status"],
            "has_incumbent": bool(payload["layout"]),
            "selected_angle_degrees": payload["selected_angle_degrees"],
            "selected_pattern": payload["selected_pattern"],
            "lcoe_cny_per_kwh": payload["evaluation"]["lcoe_cny_per_kwh"],
            "annual_energy_mwh": payload["evaluation"]["annual_energy_mwh"],
            "end_to_end_seconds": payload["end_to_end_seconds"],
            "scientific_hash": payload["scientific_hash"],
        })
        write_json(root/"formal"/"summary.partial.json", {
            "status": "running", "source_commit": arguments.source_commit,
            "completed_roles": len(rows), "expected_roles": 31, "rows": rows,
        })
    require(len(rows) == 31, "formal 31-role matrix")
    summary = {
        "status": "pass", "source_commit": arguments.source_commit,
        "paper_native_deterministic_roles": 31,
        "random_seed_repeats": "not_applicable",
        "workers_per_role": arguments.total_workers,
        "angles_per_role": 18, "patterns_per_role": 10,
        "maximum_bda_iterations": arguments.maximum_bda_iterations,
        "mip_time_limit_seconds_per_solve":
            arguments.formal_mip_time_limit_seconds,
        "rows": rows,
        "claim_boundary": (
            "source-backed flexible academic reconstruction; not author code, "
            "private arrays, Gurobi or numerical replay"
        ),
    }
    write_json(root/"formal"/"summary.json", summary)
    return summary


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output-root", type=Path, required=True)
    parser.add_argument("--source-commit", required=True)
    parser.add_argument("--total-workers", type=int, required=True)
    parser.add_argument("--h6-mip-time-limit-seconds", type=float, default=60.0)
    parser.add_argument(
        "--formal-mip-time-limit-seconds", type=float, default=10000.0
    )
    parser.add_argument("--maximum-bda-iterations", type=int, default=20)
    parser.add_argument("--phase", choices=("all", "h6", "formal"), default="all")
    arguments = parser.parse_args()
    arguments.binary = arguments.binary.resolve()
    root = arguments.output_root.resolve()
    root.mkdir(parents=True, exist_ok=True)
    cases = list_cases(arguments.binary)
    h6 = run_h6(arguments, root) if arguments.phase in ("all", "h6") else None
    formal = (
        run_formal(arguments, root, cases)
        if arguments.phase in ("all", "formal") else None
    )
    if arguments.phase != "all":
        print(json.dumps({
            "status": "pass", "phase": arguments.phase,
            "output_root": str(root),
        }, sort_keys=True))
        return
    assert h6 is not None and formal is not None
    write_json(root/"campaign_summary.json", {
        "status": "pass", "h6_status": h6["status"],
        "formal_roles": len(formal["rows"]),
        "output_root": str(root),
    })
    print(json.dumps({
        "status": "pass", "h6_speedup": h6["speedup"]["end_to_end"],
        "formal_roles": len(formal["rows"]), "output_root": str(root),
    }, sort_keys=True))


if __name__ == "__main__":
    main()
