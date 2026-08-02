#!/usr/bin/env python3
"""Resumable T19 all-core H6 and 112-role paper-native campaign.

The campaign executes the paper's own algorithm/problem pairs: four
historical comparison roles and both wind regimes across the realistic
100/400/2,500-cell K grids. Each role is deterministic, so it is executed
once rather than being mislabelled as a 25-seed stochastic experiment.
Official sequential TRW-S stays one-thread per role; all Waffle cores are
filled with independent roles. H6 separately measures one/all-core matrix,
triplet-generation and nonlinear-AEP stages at fixed mathematical work.
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
from statistics import median


METHOD = "t19_srmp_trws_declared_triplet_reconstruction_v1"
PROTOCOL = "t19_112_deterministic_paper_roles_v1"


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


def role_id(role: dict[str, Any]) -> str:
    return (
        f"{role['family']}-{role['wind']}-n{role['cells']}-k{role['turbines']}"
    )


def roles() -> list[dict[str, Any]]:
    result = [
        {"family": "historical", "wind": "wr1", "cells": 100, "turbines": 26},
        {"family": "historical", "wind": "wr1", "cells": 100, "turbines": 30},
        {"family": "historical", "wind": "wr36", "cells": 100, "turbines": 15},
        {"family": "historical", "wind": "wr36", "cells": 100, "turbines": 39},
    ]
    for wind in ("wr1", "wr36"):
        for turbines in range(10, 101, 10):
            for cells in (100, 400, 2500):
                result.append({
                    "family": "realistic", "wind": wind,
                    "cells": cells, "turbines": turbines,
                })
        for turbines in range(120, 401, 20):
            result.append({
                "family": "realistic", "wind": wind,
                "cells": 400, "turbines": turbines,
            })
        for turbines in range(120, 281, 20):
            result.append({
                "family": "realistic", "wind": wind,
                "cells": 2500, "turbines": turbines,
            })
    assert len(result) == 112
    assert len({role_id(item) for item in result}) == 112
    return result


def execute(
    *, binary: Path, output: Path, source_commit: str, role: dict[str, Any],
    workers: int, iterations: int, time_limit: float, triplets: int | None,
    one_swap: bool = True,
) -> dict[str, Any]:
    identity = {
        "source_commit": source_commit,
        "binary_sha256": sha256(binary),
        "formal_role_id": role_id(role),
        "formal_workers": workers,
        "formal_maximum_iterations": iterations,
        "formal_time_limit_seconds": time_limit,
    }
    if output.exists():
        previous = json.loads(output.read_text(encoding="utf-8"))
        if all(previous.get(key) == value for key, value in identity.items()):
            return previous
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix(".binary.tmp")
    command = [
        str(binary), "--action", "solve", "--family", role["family"],
        "--wind", role["wind"], "--cells", str(role["cells"]),
        "--turbines", str(role["turbines"]), "--workers", str(workers),
        "--iterations", str(iterations), "--time-limit", str(time_limit),
        "--one-swap", "on" if one_swap else "off",
        "--output", str(temporary),
    ]
    if triplets is not None:
        command.extend(("--triplets", str(triplets)))
    started = time.monotonic()
    subprocess.run(command, check=True, timeout=time_limit + 1800.0)
    payload = json.loads(temporary.read_text(encoding="utf-8"))
    temporary.unlink()
    payload.update(identity)
    payload.update({
        "runner_wall_seconds": time.monotonic() - started,
    })
    write_json(output, payload)
    return payload


def validate(payload: dict[str, Any]) -> None:
    if payload["method_semantic_id"] != METHOD:
        raise RuntimeError("T19 method identity mismatch")
    if payload["protocol_semantic_id"] != PROTOCOL:
        raise RuntimeError("T19 protocol identity mismatch")
    if not payload["exact_cardinality"] or not payload["spacing_feasible"]:
        raise RuntimeError(f"T19 infeasible output {payload['formal_role_id']}")
    if payload["physical_fes"] != 1 or payload["aep_gwh"] <= 0.0:
        raise RuntimeError(f"T19 invalid physics {payload['formal_role_id']}")
    expected_triplets = 0 if payload["cell_count"] == 2500 else 5000
    if payload["generated_triplets"] != expected_triplets:
        raise RuntimeError(f"T19 triplet protocol {payload['formal_role_id']}")


def run_h6(arguments: argparse.Namespace, root: Path) -> dict[str, Any]:
    # 2,500-cell WR-36 makes interaction construction and posterior AEP
    # substantial while one solver sweep keeps admission time bounded.
    fixed_role = {
        "family": "realistic", "wind": "wr36", "cells": 2500,
        "turbines": 100,
    }
    matrix_records: dict[int, list[dict[str, Any]]] = {}
    for workers in (1, arguments.total_workers):
        matrix_records[workers] = [
            execute(
                binary=arguments.binary,
                output=root / "h6" / f"matrix-r{repeat}-w{workers:02d}.json",
                source_commit=arguments.source_commit,
                role=fixed_role, workers=workers, iterations=1,
                time_limit=600.0, triplets=0, one_swap=False,
            )
            for repeat in range(1, 4)
        ]
    triplet_role = {
        "family": "realistic", "wind": "wr36", "cells": 400,
        "turbines": 100,
    }
    triplet_records: dict[int, list[dict[str, Any]]] = {}
    for workers in (1, arguments.total_workers):
        triplet_records[workers] = [
            execute(
                binary=arguments.binary,
                output=root / "h6" / f"triplet-r{repeat}-w{workers:02d}.json",
                source_commit=arguments.source_commit,
                role=triplet_role, workers=workers, iterations=1,
                time_limit=600.0, triplets=5000, one_swap=False,
            )
            for repeat in range(1, 4)
        ]
    reference = matrix_records[1][0]
    for group in (matrix_records, triplet_records):
        group_reference = group[1][0]
        for records in group.values():
            for record in records:
                for field in (
                    "layout", "scientific_hash", "aep_gwh",
                    "qip_wake_objective",
                ):
                    if record[field] != group_reference[field]:
                        raise RuntimeError(
                            f"T19 H6 one/all-worker identity {field}"
                        )
    parallel = matrix_records[arguments.total_workers][0]
    if parallel["observed_workers"] < 2:
        raise RuntimeError("T19 H6 parallel workers not observed")
    def stage_median(
        group: dict[int, list[dict[str, Any]]], workers: int, field: str
    ) -> float:
        return median(record[field] for record in group[workers])

    speedup = {
        "interaction_assembly": (
            stage_median(matrix_records, 1, "interaction_assembly_seconds")
            / stage_median(
                matrix_records, arguments.total_workers,
                "interaction_assembly_seconds",
            )
        ),
        "triplet_generation": (
            stage_median(triplet_records, 1, "triplet_generation_seconds")
            / stage_median(
                triplet_records, arguments.total_workers,
                "triplet_generation_seconds",
            )
        ),
        "nonlinear_aep": (
            stage_median(matrix_records, 1, "nonlinear_aep_seconds")
            / stage_median(
                matrix_records, arguments.total_workers,
                "nonlinear_aep_seconds",
            )
        ),
        "end_to_end": (
            stage_median(matrix_records, 1, "end_to_end_seconds")
            / stage_median(
                matrix_records, arguments.total_workers,
                "end_to_end_seconds",
            )
        ),
    }
    if speedup["interaction_assembly"] is None or speedup["interaction_assembly"] <= 1.0:
        raise RuntimeError("T19 interaction assembly did not accelerate")
    if speedup["nonlinear_aep"] <= 1.0:
        raise RuntimeError("T19 nonlinear AEP did not accelerate")
    summary = {
        "status": "pass",
        "source_commit": arguments.source_commit,
        "fixed_work": {
            "matrix_and_aep": "WR-36, 2,500 cells, K=100, no triplets, one official TRW-S sweep",
            "triplet_generation": "WR-36, 400 cells, K=100, 5,000 triplets, one official TRW-S sweep",
            "timing_repeats": 3,
        },
        "one_worker": {
            "matrix_and_aep": matrix_records[1],
            "triplet_generation": triplet_records[1],
        },
        "all_worker": {
            "matrix_and_aep": matrix_records[arguments.total_workers],
            "triplet_generation": triplet_records[arguments.total_workers],
        },
        "reference_scientific_hash": reference["scientific_hash"],
        "speedup": speedup,
        "interpretation": (
            "interaction assembly and posterior nonlinear AEP are inner-role "
            "parallel; official ordered TRW-S is sequential and independent "
            "formal roles provide the outer all-core axis"
        ),
    }
    write_json(root / "h6" / "summary.json", summary)
    return summary


def run_formal(arguments: argparse.Namespace, root: Path) -> dict[str, Any]:
    target_roles = roles()

    def one(role: dict[str, Any]) -> dict[str, Any]:
        payload = execute(
            binary=arguments.binary,
            output=root / "formal" / f"{role_id(role)}.json",
            source_commit=arguments.source_commit,
            role=role, workers=1,
            iterations=arguments.formal_iterations,
            time_limit=arguments.formal_time_limit,
            triplets=None, one_swap=True,
        )
        validate(payload)
        return payload

    completed: list[dict[str, Any]] = []
    with ThreadPoolExecutor(max_workers=arguments.total_workers) as pool:
        future_to_role = {pool.submit(one, role): role for role in target_roles}
        for future in as_completed(future_to_role):
            completed.append(future.result())
            write_json(root / "formal" / "progress.json", {
                "status": "running",
                "source_commit": arguments.source_commit,
                "completed": len(completed),
                "total": len(target_roles),
                "last_completed": completed[-1]["formal_role_id"],
            })
    completed.sort(key=lambda item: item["formal_role_id"])
    summary = {
        "status": "pass",
        "source_commit": arguments.source_commit,
        "paper_role_count": len(target_roles),
        "successful_roles": len(completed),
        "failed_roles": 0,
        "deterministic_runs_per_role": 1,
        "outer_parallel_processes": arguments.total_workers,
        "inner_workers_per_role": 1,
        "maximum_iterations": arguments.formal_iterations,
        "time_limit_seconds_per_role": arguments.formal_time_limit,
        "total_physical_fes": sum(item["physical_fes"] for item in completed),
        "roles": completed,
    }
    write_json(root / "formal" / "summary.json", summary)
    write_json(root / "formal" / "progress.json", {
        "status": "pass", "source_commit": arguments.source_commit,
        "completed": len(completed), "total": len(target_roles),
    })
    return summary


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--source-commit", required=True)
    parser.add_argument("--total-workers", type=int, required=True)
    parser.add_argument("--stage", choices=("h6", "formal", "all"), default="all")
    parser.add_argument("--formal-iterations", type=int, default=10000)
    parser.add_argument("--formal-time-limit", type=float, default=3600.0)
    arguments = parser.parse_args()
    arguments.binary = arguments.binary.resolve()
    root = arguments.output.resolve()
    root.mkdir(parents=True, exist_ok=True)
    if arguments.total_workers < 2:
        raise RuntimeError("T19 formal/H6 requires multicore execution")
    if arguments.stage in ("h6", "all"):
        run_h6(arguments, root)
    if arguments.stage in ("formal", "all"):
        run_formal(arguments, root)


if __name__ == "__main__":
    main()
