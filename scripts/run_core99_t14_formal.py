#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: resumable T14 paper-native formal campaign runner
Paper/DOI: Massive Simplification of the Wind Farm Layout Optimization
Problem; 10.5194/wes-4-663-2019
Public source: https://github.com/byuflowlab/stanley2019-variable-reduction
revision 62b590065f9541c4296338b3f1a0ee07cfcd28bc
Missing/reconstruction/claim boundary:
hpc/core99_cpp/include/core99/stanley_t14.hpp
Formal protocol: seven unique cases, direct/grid/boundary-grid, 100 starts each;
the unavailable author SNOPT is never claimed and the admitted deterministic
parallel reconstruction is used consistently
Reuse rule: an already admitted identical case/algorithm/seed/budget receipt is
registered by path and is not rerun
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
"""

from __future__ import annotations

import argparse
import json
import subprocess
import time
from pathlib import Path
from typing import Any


CASES = [
    "t14_spacing4_amalia_north_island",
    "t14_spacing6_amalia_north_island",
    "t14_spacing8_amalia_north_island",
    "t14_spacing4_amalia_ukiah",
    "t14_spacing4_amalia_victorville",
    "t14_spacing4_circle_north_island",
    "t14_spacing4_square_north_island",
]
ALGORITHMS = {
    "t14_direct": 160,
    "t14_grid": 240,
    "t14_boundary_grid": 240,
}


def write_manifest(path: Path, manifest: dict[str, Any]) -> None:
    temporary = path.with_suffix(".tmp")
    temporary.write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    temporary.replace(path)


def valid_receipt(
    receipt: dict[str, Any],
    algorithm: str,
    case: str,
    seed: int,
    budget: int,
    workers: int,
) -> bool:
    return (
        receipt.get("algorithm_id") == algorithm
        and receipt.get("problem_id") == case
        and receipt.get("seed") == seed
        and receipt.get("physical_fes") == budget
        and receipt.get("observed_workers") == workers
        and receipt.get("best_constraint_violation_m", float("inf")) <= 1.0e-5
    )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output-root", type=Path, required=True)
    parser.add_argument("--workers", type=int, required=True)
    parser.add_argument("--starts", type=int, default=100)
    parser.add_argument("--base-seed", type=int, default=20260731)
    parser.add_argument("--source-commit", required=True)
    parser.add_argument("--reuse-admission-root", type=Path)
    arguments = parser.parse_args()
    arguments.output_root.mkdir(parents=True, exist_ok=True)
    manifest_path = arguments.output_root / "manifest.json"
    if manifest_path.exists():
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        if (
            manifest["workers"] != arguments.workers
            or manifest["starts"] != arguments.starts
            or manifest["base_seed"] != arguments.base_seed
        ):
            raise SystemExit("T14 formal resume contract mismatch")
    else:
        manifest = {
            "campaign": "T14 paper-native formal",
            "problem_semantic_id": "t14_stanley_2019_seven_unique_cases_v1",
            "method_semantic_id": "t14_boundary_grid_parameterization_v1",
            "source_commit": arguments.source_commit,
            "workers": arguments.workers,
            "starts": arguments.starts,
            "base_seed": arguments.base_seed,
            "total_runs": len(CASES) * len(ALGORITHMS) * arguments.starts,
            "completed_runs": 0,
            "reused_runs": 0,
            "status": "running",
            "runs": {},
        }

    admission_manifest: dict[str, Any] | None = None
    if arguments.reuse_admission_root:
        path = arguments.reuse_admission_root / "manifest.json"
        if path.exists():
            admission_manifest = json.loads(path.read_text(encoding="utf-8"))

    for case in CASES:
        for algorithm, budget in ALGORITHMS.items():
            for start in range(arguments.starts):
                seed = arguments.base_seed + start
                key = f"{case}/{algorithm}/seed-{seed}"
                existing = manifest["runs"].get(key)
                if existing and existing.get("status") == "pass":
                    continue

                if (
                    admission_manifest
                    and case == admission_manifest.get("case")
                    and seed == admission_manifest.get("seed")
                    and algorithm in admission_manifest.get("runs", {})
                ):
                    prior = admission_manifest["runs"][algorithm]
                    if (
                        prior.get("physical_fes") == budget
                        and prior.get("observed_workers") == arguments.workers
                        and prior.get("best_constraint_violation_m", float("inf"))
                        <= 1.0e-5
                    ):
                        manifest["runs"][key] = {
                            "status": "pass",
                            "reused": True,
                            "receipt": str(
                                arguments.reuse_admission_root
                                / f"{algorithm}-w{arguments.workers}.json"
                            ),
                            **prior,
                        }
                        manifest["completed_runs"] += 1
                        manifest["reused_runs"] += 1
                        write_manifest(manifest_path, manifest)
                        continue

                directory = arguments.output_root / case / algorithm
                directory.mkdir(parents=True, exist_ok=True)
                output = directory / f"seed-{seed}.json"
                if output.exists():
                    receipt = json.loads(output.read_text(encoding="utf-8"))
                    if valid_receipt(
                        receipt,
                        algorithm,
                        case,
                        seed,
                        budget,
                        arguments.workers,
                    ):
                        manifest["runs"][key] = {
                            "status": "pass",
                            "reused": True,
                            "receipt": str(output),
                            "best_final_aep_gwh":
                                receipt["best_final_aep_gwh"],
                            "best_optimization_aep_gwh":
                                receipt["best_optimization_aep_gwh"],
                            "best_constraint_violation_m":
                                receipt["best_constraint_violation_m"],
                            "physical_fes": receipt["physical_fes"],
                            "observed_workers": receipt["observed_workers"],
                            "scientific_hash": receipt["scientific_hash"],
                            "end_to_end_seconds":
                                receipt["end_to_end_seconds"],
                        }
                        manifest["completed_runs"] += 1
                        manifest["reused_runs"] += 1
                        write_manifest(manifest_path, manifest)
                        continue
                    raise SystemExit(f"invalid existing T14 receipt: {output}")

                command = [
                    str(arguments.binary),
                    "--algorithm",
                    algorithm,
                    "--case",
                    case,
                    "--seed",
                    str(seed),
                    "--physical-fes-limit",
                    str(budget),
                    "--workers",
                    str(arguments.workers),
                    "--output",
                    str(output),
                ]
                started = time.perf_counter()
                completed = subprocess.run(
                    command,
                    text=True,
                    capture_output=True,
                    check=False,
                )
                wall = time.perf_counter() - started
                output.with_suffix(".stdout").write_text(
                    completed.stdout,
                    encoding="utf-8",
                )
                output.with_suffix(".stderr").write_text(
                    completed.stderr,
                    encoding="utf-8",
                )
                if completed.returncode != 0:
                    manifest["status"] = "failed"
                    manifest["runs"][key] = {
                        "status": "failed",
                        "returncode": completed.returncode,
                        "wall_seconds": wall,
                    }
                    write_manifest(manifest_path, manifest)
                    raise SystemExit(completed.returncode)
                receipt = json.loads(output.read_text(encoding="utf-8"))
                if not valid_receipt(
                    receipt,
                    algorithm,
                    case,
                    seed,
                    budget,
                    arguments.workers,
                ):
                    manifest["status"] = "failed"
                    manifest["runs"][key] = {
                        "status": "failed_admission",
                        "receipt": str(output),
                    }
                    write_manifest(manifest_path, manifest)
                    raise SystemExit(f"T14 formal receipt failed: {key}")
                manifest["runs"][key] = {
                    "status": "pass",
                    "reused": False,
                    "receipt": str(output),
                    "best_final_aep_gwh": receipt["best_final_aep_gwh"],
                    "best_optimization_aep_gwh":
                        receipt["best_optimization_aep_gwh"],
                    "best_constraint_violation_m":
                        receipt["best_constraint_violation_m"],
                    "physical_fes": receipt["physical_fes"],
                    "observed_workers": receipt["observed_workers"],
                    "scientific_hash": receipt["scientific_hash"],
                    "end_to_end_seconds": receipt["end_to_end_seconds"],
                    "wall_seconds": wall,
                }
                manifest["completed_runs"] += 1
                write_manifest(manifest_path, manifest)
                print(
                    "t14_formal"
                    f" completed={manifest['completed_runs']}"
                    f"/{manifest['total_runs']}"
                    f" case={case} algorithm={algorithm}"
                    f" start={start + 1}/{arguments.starts}"
                    f" wall={wall:.6f}",
                    flush=True,
                )

    manifest["status"] = "pass"
    write_manifest(manifest_path, manifest)


if __name__ == "__main__":
    main()
