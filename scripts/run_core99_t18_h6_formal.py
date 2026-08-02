#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: resumable T18 Waffle H6 and 25-seed native campaign
Paper/DOI: Reddy 2020; 10.1016/j.apenergy.2020.115090.
Formal protocol: 48 Tables-2/3 validation roles plus six Table-4 roles per
seed; 25 platform seeds; SOHO population 100, 200 generations, stagnation 20,
64 equal-area rotor points, paper RBF terrain and all available Waffle cores.
Fact boundary: hpc/core99_cpp/include/core99/reddy_t18.hpp.
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


METHOD = "t18_soho_three_kernel_relay_declared_v1"
PROBLEM = "t18_windflo_awec25_two_case_two_wake_v1"
PROTOCOL = "t18_tables2_3_4_54roles_25seed_v1"


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
    *, binary: Path, output: Path, seed: int, workers: int,
    population: int, generations: int, stagnation: int, disk_points: int,
    source_commit: str,
) -> dict[str, Any]:
    identity = {
        "source_commit": source_commit,
        "seed": seed,
        "requested_workers": workers,
        "population": population,
        "generations": generations,
        "stagnation_generations": stagnation,
        "disk_quadrature_points": disk_points,
        "validation_disk_quadrature_points": 1000,
        "terrain_profile": "paper_local_rbf",
        "disk_sampling": "paper_area_correct",
        "validation_disk_sampling": "source_uniform_radius",
        "case_id": "awec25",
    }
    if output.exists():
        previous = json.loads(output.read_text(encoding="utf-8"))
        if all(previous.get(key) == value for key, value in identity.items()):
            return previous
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix(".binary.tmp")
    started = time.monotonic()
    subprocess.run([
        str(binary), "--seed", str(seed), "--workers", str(workers),
        "--population", str(population), "--generations", str(generations),
        "--stagnation-generations", str(stagnation),
        "--disk-quadrature-points", str(disk_points),
        "--validation-disk-quadrature-points", "1000",
        "--terrain-profile", "paper_local_rbf",
        "--disk-sampling", "paper_area_correct",
        "--validation-disk-sampling", "source_uniform_radius",
        "--output", str(temporary),
    ], check=True, timeout=86_400.0)
    payload = json.loads(temporary.read_text(encoding="utf-8"))
    temporary.unlink()
    payload.update({
        "source_commit": source_commit,
        "binary_sha256": sha256(binary),
        "runner_wall_seconds": time.monotonic() - started,
    })
    write_json(output, payload)
    return payload


def validate(payload: dict[str, Any], workers: int) -> None:
    require(payload["method_semantic_id"] == METHOD, "method")
    require(payload["problem_semantic_id"] == PROBLEM, "problem")
    require(payload["protocol_semantic_id"] == PROTOCOL, "protocol")
    require(payload["requested_workers"] == workers, "workers")
    require(payload["terrain_profile"] == "paper_local_rbf", "terrain")
    require(payload["disk_sampling"] == "paper_area_correct", "disk")
    require(payload["validation_disk_sampling"] == "source_uniform_radius",
            "validation disk")
    require(payload["validation_disk_quadrature_points"] == 1000,
            "validation quadrature")
    require(len(payload["validation"]) == 48, "Tables 2 and 3 roles")
    require(len(payload["roles"]) == 6, "Table 4 roles")
    expected_work = 2 + 4 * payload["population"] * (
        payload["generations"] + 1
    )
    require(payload["objective_evaluations"] == expected_work,
            "complete-layout evaluation count")
    require(payload["wind_scenario_layout_evaluations"] > 0, "wind work")
    require(payload["wake_pair_checks"] > 0, "wake pair work")
    require(payload["disk_quadrature_samples"] > 0, "rotor quadrature work")
    require(all(len(role["layout"]) == 25 for role in payload["roles"]),
            "25-turbine roles")
    if workers > 1:
        require(payload["observed_workers"] == workers,
                "all requested workers participated")


def scientific_projection(payload: dict[str, Any]) -> dict[str, Any]:
    excluded = {
        "source_commit", "binary_sha256", "runner_wall_seconds",
        "requested_workers", "observed_workers", "parallel_regions",
        "terrain_precompute_seconds", "validation_seconds",
        "evaluator_seconds", "algorithm_seconds", "end_to_end_seconds",
    }
    return {key: value for key, value in payload.items() if key not in excluded}


def run_h6(arguments: argparse.Namespace, root: Path) -> dict[str, Any]:
    pair: dict[int, dict[str, Any]] = {}
    for workers in (1, arguments.total_workers):
        payload = optimize(
            binary=arguments.binary,
            output=root / "h6" / f"primary-w{workers:02d}.json",
            seed=18001, workers=workers, population=100, generations=2,
            stagnation=2, disk_points=64,
            source_commit=arguments.source_commit,
        )
        validate(payload, workers)
        pair[workers] = payload
    serial = pair[1]
    parallel = pair[arguments.total_workers]
    require(scientific_projection(serial) == scientific_projection(parallel),
            "H6 one/all-core scientific identity")
    speedup = {
        "end_to_end": serial["end_to_end_seconds"]
            / parallel["end_to_end_seconds"],
        "algorithm_wall": serial["algorithm_seconds"]
            / parallel["algorithm_seconds"],
        "validation_wall": serial["validation_seconds"]
            / parallel["validation_seconds"],
    }
    require(speedup["end_to_end"] > 1.0, "end-to-end speedup")
    summary = {
        "status": "pass",
        "source_commit": arguments.source_commit,
        "fixed_work": {
            "paper_problem": "25 turbines on the 2 km by 2 km AWEC terrain",
            "native_roles": 54,
            "population": 100,
            "generations": 2,
            "disk_quadrature_points": 64,
        },
        "one_worker": serial,
        "all_worker": parallel,
        "speedup": speedup,
        "same_roles_layouts_work_and_hash": True,
    }
    write_json(root / "h6" / "summary.json", summary)
    return summary


def run_formal(arguments: argparse.Namespace, root: Path) -> dict[str, Any]:
    rows: list[dict[str, Any]] = []
    for seed_index in range(arguments.seeds):
        seed = 1_800_100 + seed_index
        payload = optimize(
            binary=arguments.binary,
            output=root / "formal" / f"seed-{seed_index + 1:02d}.json",
            seed=seed, workers=arguments.total_workers,
            population=arguments.population,
            generations=arguments.generations,
            stagnation=arguments.stagnation_generations,
            disk_points=arguments.disk_quadrature_points,
            source_commit=arguments.source_commit,
        )
        validate(payload, arguments.total_workers)
        rows.append({
            "seed_index": seed_index + 1,
            "seed": seed,
            "paper_roles": 54,
            "objective_evaluations": payload["objective_evaluations"],
            "wind_scenario_layout_evaluations":
                payload["wind_scenario_layout_evaluations"],
            "wake_pair_checks": payload["wake_pair_checks"],
            "disk_quadrature_samples": payload["disk_quadrature_samples"],
            "end_to_end_seconds": payload["end_to_end_seconds"],
            "scientific_hash": payload["scientific_hash"],
        })
        write_json(root / "formal" / "summary.partial.json", {
            "status": "running",
            "source_commit": arguments.source_commit,
            "completed_runs": len(rows),
            "expected_runs": arguments.seeds,
            "completed_paper_roles": 54 * len(rows),
            "rows": rows,
        })
    summary = {
        "status": "pass",
        "source_commit": arguments.source_commit,
        "seeds": arguments.seeds,
        "paper_roles_per_seed": 54,
        "completed_runs": len(rows),
        "completed_paper_roles": 54 * len(rows),
        "objective_evaluations": sum(
            row["objective_evaluations"] for row in rows
        ),
        "wind_scenario_layout_evaluations": sum(
            row["wind_scenario_layout_evaluations"] for row in rows
        ),
        "wake_pair_checks": sum(row["wake_pair_checks"] for row in rows),
        "disk_quadrature_samples": sum(
            row["disk_quadrature_samples"] for row in rows
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
    parser.add_argument("--total-workers", required=True, type=int)
    parser.add_argument("--stage", choices=("h6", "formal", "all"), default="all")
    parser.add_argument("--seeds", type=int, default=25)
    parser.add_argument("--population", type=int, default=100)
    parser.add_argument("--generations", type=int, default=200)
    parser.add_argument("--stagnation-generations", type=int, default=20)
    parser.add_argument("--disk-quadrature-points", type=int, default=64)
    arguments = parser.parse_args()
    arguments.binary = arguments.binary.resolve()
    require(arguments.total_workers >= 2, "H6 requires multiple cores")
    arguments.output.mkdir(parents=True, exist_ok=True)
    write_json(arguments.output / "manifest.json", {
        "corpus_id": "T18",
        "source_commit": arguments.source_commit,
        "binary": str(arguments.binary),
        "binary_sha256": sha256(arguments.binary),
        "total_workers": arguments.total_workers,
        "stage": arguments.stage,
        "formal_budget": {
            "seeds": arguments.seeds,
            "paper_problem": "25 turbines on the 2 km by 2 km AWEC terrain",
            "native_roles_per_seed": 54,
            "population": arguments.population,
            "generations": arguments.generations,
            "stagnation_generations": arguments.stagnation_generations,
            "disk_quadrature_points": arguments.disk_quadrature_points,
        },
    })
    if arguments.stage in {"h6", "all"}:
        run_h6(arguments, arguments.output)
    if arguments.stage in {"formal", "all"}:
        require((arguments.output / "h6" / "summary.json").exists(),
                "formal run requires completed H6 summary")
        run_formal(arguments, arguments.output)


if __name__ == "__main__":
    main()
