#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: resumable T25 Waffle H6 and 25-seed native campaign
Paper/DOI: Rodrigues et al. 2024; 10.5194/wes-9-321-2024.
Formal protocol: both paper problem families; target exact-gradient,
flow-parallel, top-level multi-start, SMAST and SLSQP mechanisms; 65 unique
paper roles are mapped, 55 target/platform roles are executable, and ten
complex-step or spacing-cost comparator/diagnostic roles remain explicitly
observation-only; 25 platform seeds cover the 30 stochastic IEA roles;
deterministic Horns performance roles are measured once. Public
55,000-plus-run NetCDF arrays remain separate author evidence.
Stopping boundary: public tensors store 5000 SciPy iterations; the production
replacement independently caps NLopt at 5000 objective callbacks and records
actual objective, gradient and physical-layout counts.
Fact boundary: hpc/core99_cpp/include/core99/rodrigues_t25.hpp.
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
"""

from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time
from typing import Any


METHOD = "t25_smast_slsqp_exact_reverse_v1"
PROBLEM_IEA = "t25_iea37_scaled_sbg_v1"
PROBLEM_HORNS = "t25_hornsrev_scaled_bg_v80_v1"
PROTOCOL = "t25_65role_public_data_25seed_v1"


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


def execute(
    *, binary: Path, output: Path, source_commit: str,
    arguments: list[str], identity: dict[str, Any], timeout: float,
) -> dict[str, Any]:
    expected = {"source_commit": source_commit, **identity}
    if output.exists():
        previous = json.loads(output.read_text(encoding="utf-8"))
        if all(previous.get(key) == value for key, value in expected.items()):
            return previous
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix(".binary.tmp")
    started = time.monotonic()
    subprocess.run(
        [str(binary), *arguments, "--output", str(temporary)],
        check=True, timeout=timeout,
    )
    payload = json.loads(temporary.read_text(encoding="utf-8"))
    temporary.unlink()
    payload.update(expected)
    payload.update({
        "binary_sha256": sha256(binary),
        "runner_wall_seconds": time.monotonic() - started,
    })
    write_json(output, payload)
    return payload


def evaluate(
    *, binary: Path, output: Path, source_commit: str, family: str,
    turbines: int, workers: int, gradient: str, directions: int = 360,
    speeds: int = 1,
) -> dict[str, Any]:
    identity = {
        "family": family,
        "turbine_count": turbines,
        "direction_count": directions,
        "speed_count": speeds,
        "gradient_mode": gradient,
        "requested_workers": workers,
    }
    return execute(
        binary=binary, output=output, source_commit=source_commit,
        arguments=[
            "--action", "evaluate", "--family", family,
            "--turbines", str(turbines), "--directions", str(directions),
            "--speeds", str(speeds), "--workers", str(workers),
            "--gradient", gradient,
        ],
        identity=identity,
        timeout=7 * 24 * 3600.0,
    )


def smart_start(
    *, binary: Path, output: Path, source_commit: str, turbines: int,
    workers: int, seed: int, random_percent: int, grid_r: int,
) -> dict[str, Any]:
    identity = {
        "turbine_count": turbines,
        "requested_workers": workers,
        "seed": seed,
        "random_percent": random_percent,
        "grid_resolution_rotor_radii": float(grid_r),
    }
    return execute(
        binary=binary, output=output, source_commit=source_commit,
        arguments=[
            "--action", "smart-start", "--family", "iea37",
            "--turbines", str(turbines), "--directions", "360",
            "--workers", str(workers), "--seed", str(seed),
            "--random-percent", str(random_percent), "--grid-r", str(grid_r),
        ],
        identity=identity,
        timeout=7 * 24 * 3600.0,
    )


def optimize(
    *, binary: Path, output: Path, source_commit: str, turbines: int,
    seed: int, start_index: int, random_percent: int, grid_r: int,
    maximum_evaluations: int, smart: bool,
) -> dict[str, Any]:
    identity = {
        "turbine_count": turbines,
        "direction_count": 360,
        "speed_count": 1,
        "requested_workers": 1,
        "seed": seed,
        "start_index": start_index,
        "random_percent": random_percent,
        "formal_maximum_objective_callbacks": maximum_evaluations,
        "initialization_profile": "smast" if smart else "random",
    }
    return execute(
        binary=binary, output=output, source_commit=source_commit,
        arguments=[
            "--action", "optimize", "--family", "iea37",
            "--turbines", str(turbines), "--directions", "360",
            "--workers", "1", "--seed", str(seed),
            "--start-index", str(start_index),
            "--random-percent", str(random_percent),
            "--grid-r", str(grid_r),
            "--max-evaluations", str(maximum_evaluations),
            "--xtol-rel", "1e-4",
            "--initialization", "smast" if smart else "random",
        ],
        identity=identity,
        timeout=7 * 24 * 3600.0,
    )


def validate_evaluation(payload: dict[str, Any], problem: str) -> None:
    require(payload["problem_semantic_id"] == problem, "problem identity")
    require(payload["method_semantic_id"] == METHOD, "method identity")
    require(payload["aep_gwh"] > 0.0, "positive AEP")
    require(payload["flow_cases"] > 0, "flow work")
    require(payload["pair_interactions"] > 0, "pair work")
    if payload["requested_workers"] > 1:
        require(payload["observed_workers"] >= 2, "parallel participation")


def run_h6(arguments: argparse.Namespace, root: Path) -> dict[str, Any]:
    fixed: dict[str, dict[int, dict[str, Any]]] = {
        "evaluator": {}, "exact_gradient": {}, "smast": {}
    }
    for workers in (1, arguments.total_workers):
        fixed["evaluator"][workers] = evaluate(
            binary=arguments.binary,
            output=root / "h6" / f"horns100-evaluator-w{workers:02d}.json",
            source_commit=arguments.source_commit,
            family="horns_rev", turbines=100, workers=workers,
            gradient="none", directions=360, speeds=23,
        )
        fixed["exact_gradient"][workers] = evaluate(
            binary=arguments.binary,
            output=root / "h6" / f"horns100-gradient-w{workers:02d}.json",
            source_commit=arguments.source_commit,
            family="horns_rev", turbines=100, workers=workers,
            gradient="exact_reverse", directions=360, speeds=23,
        )
        fixed["smast"][workers] = smart_start(
            binary=arguments.binary,
            output=root / "h6" / f"iea64-smast-w{workers:02d}.json",
            source_commit=arguments.source_commit,
            turbines=64, workers=workers, seed=25001,
            random_percent=0, grid_r=3,
        )
    for family in ("evaluator", "exact_gradient"):
        serial = fixed[family][1]
        parallel = fixed[family][arguments.total_workers]
        validate_evaluation(serial, PROBLEM_HORNS)
        validate_evaluation(parallel, PROBLEM_HORNS)
        require(serial["aep_gwh"] == parallel["aep_gwh"], f"{family} AEP identity")
        require(
            serial["maximum_abs_gradient_gwh_per_m"]
            == parallel["maximum_abs_gradient_gwh_per_m"],
            f"{family} gradient identity",
        )
    serial_smast = fixed["smast"][1]
    parallel_smast = fixed["smast"][arguments.total_workers]
    require(serial_smast["aep_gwh"] == parallel_smast["aep_gwh"], "SMAST identity")
    require(
        serial_smast["minimum_spacing_m"] == parallel_smast["minimum_spacing_m"],
        "SMAST spacing identity",
    )
    speedup = {
        "evaluator": fixed["evaluator"][1]["seconds"]
            / fixed["evaluator"][arguments.total_workers]["seconds"],
        "exact_gradient": fixed["exact_gradient"][1]["seconds"]
            / fixed["exact_gradient"][arguments.total_workers]["seconds"],
        "smast_end_to_end": fixed["smast"][1]["seconds"]
            / fixed["smast"][arguments.total_workers]["seconds"],
    }
    require(speedup["evaluator"] > 1.0, "evaluator speedup")
    require(speedup["exact_gradient"] > 1.0, "gradient speedup")
    summary = {
        "status": "pass",
        "source_commit": arguments.source_commit,
        "fixed_work": {
            "evaluator_and_gradient": "Horns Rev 100 V80 turbines, 360 directions by 23 speeds",
            "smast": "IEA-37 64 turbines, 360 directions, 3R grid, random_percent=0",
        },
        "one_worker": {key: value[1] for key, value in fixed.items()},
        "all_worker": {
            key: value[arguments.total_workers] for key, value in fixed.items()
        },
        "speedup": speedup,
        "same_physics_gradient_and_initial_layout_metrics": True,
    }
    write_json(root / "h6" / "summary.json", summary)
    return summary


def run_horns_matrix(arguments: argparse.Namespace, root: Path) -> dict[str, Any]:
    rows: list[dict[str, Any]] = []
    for turbines in (100, 200, 300, 400, 500):
        for workers in (1, arguments.total_workers):
            for gradient in ("none", "exact_reverse"):
                payload = evaluate(
                    binary=arguments.binary,
                    output=root / "formal" / "horns" /
                        f"n{turbines}-{gradient}-w{workers:02d}.json",
                    source_commit=arguments.source_commit,
                    family="horns_rev", turbines=turbines,
                    workers=workers, gradient=gradient,
                    directions=360, speeds=23,
                )
                validate_evaluation(payload, PROBLEM_HORNS)
                rows.append(payload)
        if arguments.include_fd:
            payload = evaluate(
                binary=arguments.binary,
                output=root / "formal" / "horns" /
                    f"n{turbines}-central_fd-w{arguments.total_workers:02d}.json",
                source_commit=arguments.source_commit,
                family="horns_rev", turbines=turbines,
                workers=arguments.total_workers, gradient="central_fd",
                directions=360, speeds=23,
            )
            validate_evaluation(payload, PROBLEM_HORNS)
            rows.append(payload)
        write_json(root / "formal" / "horns" / "summary.partial.json", {
            "status": "running",
            "source_commit": arguments.source_commit,
            "completed_records": len(rows),
            "rows": rows,
        })
    summary = {
        "status": "pass",
        "source_commit": arguments.source_commit,
        "completed_records": len(rows),
        "includes_fd": arguments.include_fd,
        "rows": rows,
    }
    write_json(root / "formal" / "horns" / "summary.json", summary)
    return summary


def stochastic_roles() -> list[tuple[str, int, int, bool]]:
    roles: list[tuple[str, int, int, bool]] = []
    for turbines in (16, 36, 64, 130, 279):
        for random_percent in (0, 1, 10, 50, 100):
            roles.append((f"storyline3-n{turbines}-r{random_percent}", turbines, random_percent, True))
    for random_percent in (0, 100):
        roles.append((f"storyline3-n566-r{random_percent}", 566, random_percent, True))
    for turbines in (16, 64, 279):
        roles.append((f"storyline3-100pct-n{turbines}", turbines, 100, False))
    return roles


def run_iea_campaign(arguments: argparse.Namespace, root: Path) -> dict[str, Any]:
    jobs: list[tuple[str, int, int, bool, int, int, Path]] = []
    for seed_index in range(arguments.seeds):
        seed = 2_500_100 + seed_index
        for role_index, (role, turbines, random_percent, smart) in enumerate(stochastic_roles()):
            grid_r = 5 if turbines == 566 else 3
            output = root / "formal" / "iea" / role / f"seed-{seed_index + 1:02d}.json"
            jobs.append((role, turbines, random_percent, smart, seed_index, seed, output))

    completed: list[dict[str, Any]] = []
    with ThreadPoolExecutor(max_workers=arguments.total_workers) as pool:
        futures = {}
        for role, turbines, random_percent, smart, seed_index, seed, output in jobs:
            grid_r = 5 if turbines == 566 else 3
            future = pool.submit(
                optimize,
                binary=arguments.binary, output=output,
                source_commit=arguments.source_commit,
                turbines=turbines, seed=seed,
                start_index=seed_index, random_percent=random_percent,
                grid_r=grid_r,
                maximum_evaluations=arguments.formal_maximum_evaluations,
                smart=smart,
            )
            futures[future] = (role, seed_index, output)
        for future in as_completed(futures):
            role, seed_index, output = futures[future]
            payload = future.result()
            require(payload["problem_semantic_id"] == PROBLEM_IEA, "IEA problem")
            require(payload["method_semantic_id"] == METHOD, "IEA method")
            require(payload["final_aep_gwh"] > 0.0, "IEA final AEP")
            require(payload["maximum_boundary_violation_m"] <= 1.0e-3, "IEA boundary")
            completed.append({
                "role": role,
                "seed_index": seed_index + 1,
                "path": str(output.relative_to(root)),
                "physical_layout_evaluations": payload["physical_layout_evaluations"],
                "final_aep_gwh": payload["final_aep_gwh"],
                "end_to_end_seconds": payload["end_to_end_seconds"],
                "scientific_hash": payload["scientific_hash"],
            })
            completed.sort(key=lambda row: (row["role"], row["seed_index"]))
            write_json(root / "formal" / "iea" / "summary.partial.json", {
                "status": "running",
                "source_commit": arguments.source_commit,
                "outer_parallel_workers": arguments.total_workers,
                "inner_workers_per_start": 1,
                "completed_runs": len(completed),
                "expected_runs": len(jobs),
                "rows": completed,
            })
    summary = {
        "status": "pass",
        "source_commit": arguments.source_commit,
        "protocol_semantic_id": PROTOCOL,
        "seeds": arguments.seeds,
        "stochastic_roles_per_seed": len(stochastic_roles()),
        "completed_runs": len(completed),
        "outer_parallel_workers": arguments.total_workers,
        "inner_workers_per_start": 1,
        "formal_maximum_objective_callbacks": arguments.formal_maximum_evaluations,
        "physical_layout_evaluations": sum(
            row["physical_layout_evaluations"] for row in completed
        ),
        "summed_end_to_end_seconds": sum(
            row["end_to_end_seconds"] for row in completed
        ),
        "rows": completed,
    }
    write_json(root / "formal" / "iea" / "summary.json", summary)
    return summary


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--source-commit", required=True)
    parser.add_argument("--total-workers", required=True, type=int)
    parser.add_argument("--stage", choices=("h6", "formal", "all"), default="all")
    parser.add_argument("--seeds", type=int, default=25)
    parser.add_argument("--formal-maximum-evaluations", type=int, default=5000)
    parser.add_argument("--include-fd", action=argparse.BooleanOptionalAction, default=True)
    arguments = parser.parse_args()
    arguments.binary = arguments.binary.resolve()
    require(arguments.total_workers >= 2, "T25 H6 requires multiple cores")
    arguments.output.mkdir(parents=True, exist_ok=True)
    write_json(arguments.output / "manifest.json", {
        "corpus_id": "T25",
        "source_commit": arguments.source_commit,
        "binary": str(arguments.binary),
        "binary_sha256": sha256(arguments.binary),
        "total_workers": arguments.total_workers,
        "stage": arguments.stage,
        "formal_budget": {
            "registered_unique_paper_roles": 65,
            "platform_executed_distinct_roles": 55,
            "observation_only_comparator_diagnostic_roles": 10,
            "stochastic_roles_per_seed": len(stochastic_roles()),
            "platform_seeds": arguments.seeds,
            "expected_stochastic_runs": len(stochastic_roles()) * arguments.seeds,
            "formal_maximum_objective_callbacks": arguments.formal_maximum_evaluations,
            "include_fd": arguments.include_fd,
            "public_author_result_runs": "retained separately; not relabelled",
        },
        "parallel_policy": {
            "Horns_flow_cases": f"one persistent {arguments.total_workers}-core team",
            "IEA_multistarts": f"{arguments.total_workers} concurrent one-core starts",
            "nested_oversubscription": "forbidden",
        },
    })
    if arguments.stage in {"h6", "all"}:
        run_h6(arguments, arguments.output)
    if arguments.stage in {"formal", "all"}:
        require((arguments.output / "h6" / "summary.json").exists(), "formal requires H6")
        run_horns_matrix(arguments, arguments.output)
        run_iea_campaign(arguments, arguments.output)


if __name__ == "__main__":
    main()
