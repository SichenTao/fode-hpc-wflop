#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: resumable L0298 Waffle H6 and 25-seed native campaign
Paper/DOI: 10.1109/TSG.2020.3022378.
Formal protocol: nine native profiles, 29 paper roles per seed, 25 platform
seeds, NSGA-III 120x250 and inner BPSO 100x250, all available Waffle cores.
H6 uses a fixed nontrivial 120x5 outer and 100x20 inner workload only to
establish one/all-core identity and acceleration before the formal campaign.
Public assets, missing fields, conflicts, reconstruction and claim boundary:
hpc/core99_cpp/include/core99/tao_l0298.hpp.
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


METHOD = "l0298_yarpiz_nsga3_bpso_qp_declared_v1"
PROBLEM = "l0298_offshore_grid_cable_rts24_declared_v1"
PROTOCOL = "l0298_models_turbines_seasons_buses_29roles_v1"
PROFILES = {
    "models-winter-e82-bus3": 7,
    "model1-winter-e115-bus3": 2,
    "model1-winter-ltw101-bus3": 2,
    "model1-summer-e82-bus3": 3,
    "model1-winter-e82-bus5": 3,
    "model1-winter-e82-bus7": 3,
    "model1-winter-e82-bus16": 3,
    "model1-winter-e82-bus21": 3,
    "model1-winter-e82-bus23": 3,
}


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


def optimize(
    *, binary: Path, output: Path, profile: str, seed: int, workers: int,
    outer_population: int, outer_iterations: int, inner_population: int,
    inner_iterations: int, source_commit: str,
) -> dict[str, Any]:
    identity = {
        "source_commit": source_commit,
        "profile_id": profile,
        "seed": seed,
        "requested_workers": workers,
        "outer_population": outer_population,
        "outer_iterations": outer_iterations,
        "inner_population": inner_population,
        "inner_iterations": inner_iterations,
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
        "--workers", str(workers),
        "--outer-population", str(outer_population),
        "--outer-iterations", str(outer_iterations),
        "--inner-population", str(inner_population),
        "--inner-iterations", str(inner_iterations),
        "--output", str(temporary),
    ], check=True, timeout=604_800.0)
    payload = json.loads(temporary.read_text(encoding="utf-8"))
    temporary.unlink()
    payload.update({
        "source_commit": source_commit,
        "binary_sha256": sha256(binary),
        "runner_wall_seconds": time.monotonic() - started,
    })
    write_json(output, payload)
    return payload


def validate(payload: dict[str, Any], profile: str, workers: int) -> None:
    require(payload["profile_id"] == profile, "profile")
    require(payload["method_semantic_id"] == METHOD, "method")
    require(payload["problem_semantic_id"] == PROBLEM, "problem")
    require(payload["protocol_semantic_id"] == PROTOCOL, "protocol")
    require(payload["requested_workers"] == workers, "workers")
    require(len(payload["roles"]) == PROFILES[profile], "paper role count")
    require(payload["hourly_wake_evaluations"]
            == 24 * payload["complete_outer_evaluations"], "hourly work")
    for role in payload["roles"]:
        evaluation = role["evaluation"]
        require(evaluation["feasible"] is True, "role feasibility")
        require(60 <= evaluation["turbine_count"] <= 80, "capacity range")
        require(len(role["active_cells"]) == evaluation["turbine_count"],
                "layout cardinality")
        require(len(role["cable_edges"]) == evaluation["turbine_count"],
                "radial cable cardinality")
    if workers > 1:
        require(payload["observed_workers"] == workers,
                "all requested workers participated")


def scientific_projection(payload: dict[str, Any]) -> dict[str, Any]:
    return {
        key: payload[key] for key in (
            "profile_id", "method_semantic_id", "problem_semantic_id",
            "protocol_semantic_id", "seed", "outer_population",
            "outer_iterations", "inner_population", "inner_iterations",
            "complete_outer_evaluations", "cable_particle_evaluations",
            "hourly_wake_evaluations", "scientific_hash", "roles",
        )
    }


def run_h6(arguments: argparse.Namespace, root: Path) -> dict[str, Any]:
    profile = "models-winter-e82-bus3"
    pair: dict[int, dict[str, Any]] = {}
    for workers in (1, arguments.total_workers):
        payload = optimize(
            binary=arguments.binary,
            output=root / "h6" / f"{profile}-w{workers:02d}.json",
            profile=profile, seed=29801, workers=workers,
            outer_population=120, outer_iterations=arguments.h6_outer_iterations,
            inner_population=100, inner_iterations=arguments.h6_inner_iterations,
            source_commit=arguments.source_commit,
        )
        validate(payload, profile, workers)
        pair[workers] = payload
    serial = pair[1]
    parallel = pair[arguments.total_workers]
    require(scientific_projection(serial) == scientific_projection(parallel),
            "H6 one/all-core scientific identity")
    speedup = {
        "end_to_end": serial["end_to_end_seconds"]
            / parallel["end_to_end_seconds"],
        "wake_and_coupled_evaluator": serial["wake_and_coupled_evaluator_seconds"]
            / parallel["wake_and_coupled_evaluator_seconds"],
        "evolutionary_orchestration": serial["evolutionary_orchestration_seconds"]
            / parallel["evolutionary_orchestration_seconds"],
    }
    require(speedup["end_to_end"] > 1.0, "end-to-end speedup")
    summary = {
        "status": "pass", "source_commit": arguments.source_commit,
        "profile": profile, "fixed_work": {
            "outer_population": 120,
            "outer_iterations": arguments.h6_outer_iterations,
            "inner_population": 100,
            "inner_iterations": arguments.h6_inner_iterations,
        },
        "one_worker": serial, "all_worker": parallel, "speedup": speedup,
        "same_roles_layouts_cables_objectives_work_and_hash": True,
    }
    write_json(root / "h6" / "summary.json", summary)
    return summary


def run_formal(arguments: argparse.Namespace, root: Path) -> dict[str, Any]:
    rows: list[dict[str, Any]] = []
    expected = arguments.seeds * len(PROFILES)
    for seed_index in range(arguments.seeds):
        seed = 2_980_100 + seed_index
        for profile in PROFILES:
            payload = optimize(
                binary=arguments.binary,
                output=root / "formal" / f"seed-{seed_index + 1:02d}"
                    / f"{profile}.json",
                profile=profile, seed=seed, workers=arguments.total_workers,
                outer_population=arguments.outer_population,
                outer_iterations=arguments.outer_iterations,
                inner_population=arguments.inner_population,
                inner_iterations=arguments.inner_iterations,
                source_commit=arguments.source_commit,
            )
            validate(payload, profile, arguments.total_workers)
            rows.append({
                "seed_index": seed_index + 1,
                "seed": seed,
                "profile": profile,
                "paper_roles": len(payload["roles"]),
                "complete_outer_evaluations":
                    payload["complete_outer_evaluations"],
                "cable_particle_evaluations":
                    payload["cable_particle_evaluations"],
                "hourly_wake_evaluations": payload["hourly_wake_evaluations"],
                "end_to_end_seconds": payload["end_to_end_seconds"],
                "scientific_hash": payload["scientific_hash"],
            })
            write_json(root / "formal" / "summary.partial.json", {
                "status": "running", "source_commit": arguments.source_commit,
                "completed_runs": len(rows), "expected_runs": expected,
                "completed_paper_roles": sum(row["paper_roles"] for row in rows),
                "rows": rows,
            })
    summary = {
        "status": "pass", "source_commit": arguments.source_commit,
        "seeds": arguments.seeds,
        "profiles_per_seed": len(PROFILES),
        "paper_roles_per_seed": sum(PROFILES.values()),
        "completed_runs": len(rows),
        "completed_paper_roles": sum(row["paper_roles"] for row in rows),
        "complete_outer_evaluations": sum(
            row["complete_outer_evaluations"] for row in rows
        ),
        "cable_particle_evaluations": sum(
            row["cable_particle_evaluations"] for row in rows
        ),
        "hourly_wake_evaluations": sum(
            row["hourly_wake_evaluations"] for row in rows
        ),
        "summed_end_to_end_seconds": sum(
            row["end_to_end_seconds"] for row in rows
        ),
        "rows": rows,
    }
    write_json(root / "formal" / "summary.json", summary)
    return summary


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--source-commit", required=True)
    parser.add_argument("--total-workers", type=int, required=True)
    parser.add_argument("--stage", choices=("h6", "formal", "all"), default="all")
    parser.add_argument("--seeds", type=int, default=25)
    parser.add_argument("--outer-population", type=int, default=120)
    parser.add_argument("--outer-iterations", type=int, default=250)
    parser.add_argument("--inner-population", type=int, default=100)
    parser.add_argument("--inner-iterations", type=int, default=250)
    parser.add_argument("--h6-outer-iterations", type=int, default=5)
    parser.add_argument("--h6-inner-iterations", type=int, default=20)
    arguments = parser.parse_args()
    arguments.binary = arguments.binary.resolve()
    arguments.output = arguments.output.resolve()
    require(arguments.binary.is_file(), "binary")
    require(arguments.total_workers >= 2, "total workers")
    require(arguments.seeds > 0, "seeds")
    arguments.output.mkdir(parents=True, exist_ok=True)
    result: dict[str, Any] = {
        "source_commit": arguments.source_commit,
        "binary_sha256": sha256(arguments.binary),
    }
    if arguments.stage in ("h6", "all"):
        result["h6"] = run_h6(arguments, arguments.output)
    if arguments.stage in ("formal", "all"):
        result["formal"] = run_formal(arguments, arguments.output)
    write_json(arguments.output / "campaign_summary.json", result)


if __name__ == "__main__":
    main()
