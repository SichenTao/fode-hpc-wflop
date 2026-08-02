#!/usr/bin/env python3
"""H6 and paper-native formal campaign for T08.

WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T08 immutable H6 plus all 49 proposed-method roles
Paper/DOI: 10.1016/j.apenergy.2016.06.101
Protocol: nine classical problems each use USL once and 5S/20S for ten
independent paper-declared runs; nine scaling-density problems use USL and one
random start; two land problems use USL and 5S. This is 49 named roles and 211
optimization receipts. GA, HGA and finite differences remain reported paper
comparators/diagnostics, not target algorithms silently reimplemented here.
The exact-gradient physical FES is one complete wind-resource layout plus
gradient evaluation. Formal budgets are 1500 evaluations for benchmark USL,
1000 per benchmark random start, 2500 for scaling and 3000 for land starts;
they cover or exceed the paper's reported exact-gradient ranges without
equating them to unavailable MATLAB iteration trajectories.
Full declaration: hpc/core99_cpp/include/core99/guirguis_t08.hpp
Controlling contract: shared/contracts/core99_t08_guirguis_2016.json
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import statistics
import subprocess
import time
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class Role:
    role_id: str
    case_id: str
    policy: str
    starts: int
    repeats: int
    maximum_evaluations: int
    barrier_phases: int


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def atomic_json(path: Path, payload: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n")
    os.replace(temporary, path)


def command(
    binary: Path, case_id: str, policy: str, starts: int, workers: int,
    seed: int, maximum_evaluations: int, barrier_phases: int,
    output: Path | None = None,
) -> list[str]:
    result = [
        str(binary), "--mode", "optimize", "--case", case_id,
        "--start-policy", policy, "--starts", str(starts),
        "--workers", str(workers), "--seed", str(seed),
        "--maximum-evaluations", str(maximum_evaluations),
        "--barrier-phases", str(barrier_phases),
    ]
    if output is not None:
        result.extend(["--output", str(output)])
    return result


def evaluate_command(binary: Path, workers: int) -> list[str]:
    return [
        str(binary), "--mode", "evaluate", "--case", "t08_benchmark_c3_n30",
        "--start-policy", "lhs", "--seed", "80601", "--workers", str(workers),
    ]


def capture_json(arguments: list[str]) -> dict:
    completed = subprocess.run(
        arguments, check=True, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE,
    )
    return json.loads(completed.stdout)


def roles(budget_scale: float = 1.0) -> list[Role]:
    if not 0.0 < budget_scale <= 1.0:
        raise ValueError("formal budget scale must be in (0, 1]")

    def scaled(value: int) -> int:
        return max(8, int(round(value * budget_scale)))

    result: list[Role] = []
    for wind_case in (1, 2, 3):
        for turbines in (10, 20, 30):
            case_id = f"t08_benchmark_c{wind_case}_n{turbines}"
            result.extend([
                Role(f"{case_id}_usl", case_id, "usl", 1, 1, scaled(1500), 6),
                Role(f"{case_id}_5s", case_id, "lhs", 5, 10, scaled(1000), 6),
                Role(f"{case_id}_20s", case_id, "lhs", 20, 10, scaled(1000), 6),
            ])
    for turbines in (37, 50, 100):
        for density in (4, 5, 6):
            case_id = f"t08_scaling_n{turbines}_d{density}"
            result.extend([
                Role(f"{case_id}_usl", case_id, "usl", 1, 1, scaled(2500), 7),
                Role(f"{case_id}_rsl", case_id, "lhs", 1, 1, scaled(2500), 7),
            ])
    for case_id in ("t08_land_copenhagen_n37", "t08_land_ring_n20"):
        result.extend([
            Role(f"{case_id}_usl", case_id, "usl", 1, 1, scaled(3000), 7),
            Role(f"{case_id}_5s", case_id, "lhs", 5, 1, scaled(3000), 7),
        ])
    assert len(result) == 49
    assert sum(role.repeats for role in result) == 211
    return result


def run_h6(binary: Path, output_root: Path, source_commit: str, workers: int) -> dict:
    evaluation: dict[int, list[dict]] = {1: [], workers: []}
    for worker_count in (1, workers):
        for _ in range(5):
            evaluation[worker_count].append(
                capture_json(evaluate_command(binary, worker_count))
            )
    serial_eval = [item["evaluator_seconds"] for item in evaluation[1]][1:]
    parallel_eval = [item["evaluator_seconds"] for item in evaluation[workers]][1:]
    serial_constraints = [item["constraint_seconds"] for item in evaluation[1]][1:]
    parallel_constraints = [item["constraint_seconds"]
                            for item in evaluation[workers]][1:]
    one_science = evaluation[1][-1]
    all_science = evaluation[workers][-1]

    orchestration: dict[int, list[dict]] = {1: [], workers: []}
    for worker_count in (1, workers):
        for _ in range(3):
            orchestration[worker_count].append(capture_json(command(
                binary, "t08_benchmark_c3_n30", "lhs", 20, worker_count,
                80602, 120, 3,
            )))
    serial_end = [item["end_to_end_seconds"] for item in orchestration[1]][1:]
    parallel_end = [item["end_to_end_seconds"]
                    for item in orchestration[workers]][1:]
    one_optimization = orchestration[1][-1]
    all_optimization = orchestration[workers][-1]

    identity = (
        one_science["efficiency_percent"] == all_science["efficiency_percent"]
        and one_science["maximum_abs_gradient_percent_per_m"]
            == all_science["maximum_abs_gradient_percent_per_m"]
        and one_optimization["scientific_hash"]
            == all_optimization["scientific_hash"]
    )
    receipt = {
        "schema_version": 1,
        "paper_id": "T08",
        "source_commit": source_commit,
        "binary_sha256": sha256(binary),
        "status": "pass" if identity else "fail",
        "total_workers": workers,
        "fixed_evaluator_case": "t08_benchmark_c3_n30",
        "evaluator_science_identity": identity,
        "serial_evaluator_seconds_median": statistics.median(serial_eval),
        "all_core_evaluator_seconds_median": statistics.median(parallel_eval),
        "evaluator_speedup": statistics.median(serial_eval)
            / statistics.median(parallel_eval),
        "serial_constraint_seconds_median": statistics.median(serial_constraints),
        "all_core_constraint_seconds_median": statistics.median(parallel_constraints),
        "constraint_one_to_all_timing_ratio": statistics.median(serial_constraints)
            / statistics.median(parallel_constraints),
        "constraint_dispatch_policy": "serial_below_20000_pairs_measured_fast_path",
        "constraint_hpc_claim": "none; both paths execute the selected serial kernel",
        "fixed_orchestration_case": "t08_benchmark_c3_n30_20s",
        "orchestration_science_identity": (
            one_optimization["scientific_hash"]
            == all_optimization["scientific_hash"]
        ),
        "serial_orchestration_seconds_median": statistics.median(serial_end),
        "all_core_orchestration_seconds_median": statistics.median(parallel_end),
        "orchestration_speedup": statistics.median(serial_end)
            / statistics.median(parallel_end),
        "observed_evaluator_workers": all_science["observed_workers"],
        "observed_orchestration_workers": all_optimization["observed_workers"],
        "raw_evaluation_receipts": evaluation,
        "raw_orchestration_receipts": orchestration,
    }
    atomic_json(output_root / "h6.json", receipt)
    if not identity:
        raise RuntimeError("T08 H6 scientific identity failed")
    return receipt


def run_formal(binary: Path, output_root: Path, source_commit: str,
               workers: int, base_seed: int, budget_scale: float) -> dict:
    role_list = roles(budget_scale)
    runs_root = output_root / "formal-runs"
    runs_root.mkdir(parents=True, exist_ok=True)
    binary_digest = sha256(binary)
    successes = 0
    failures: list[dict] = []
    physical_fes = 0
    start_time = time.time()
    manifest_path = output_root / "formal-manifest.json"

    for role_index, role in enumerate(role_list):
        for repeat in range(role.repeats):
            seed = base_seed + 10000 * role_index + repeat
            run_id = f"{role.role_id}__r{repeat:02d}"
            destination = runs_root / f"{run_id}.json"
            expected_identity = {
                "source_commit": source_commit,
                "binary_sha256": binary_digest,
                "role_id": role.role_id,
                "seed": seed,
                "workers": workers,
                "maximum_evaluations_per_start": role.maximum_evaluations,
                "barrier_phases": role.barrier_phases,
            }
            if destination.exists():
                try:
                    existing = json.loads(destination.read_text())
                    if existing.get("formal_identity") == expected_identity:
                        successes += 1
                        physical_fes += int(existing["physical_layout_evaluations"])
                        continue
                except (json.JSONDecodeError, KeyError, TypeError):
                    pass

            temporary = destination.with_suffix(".json.partial")
            arguments = command(
                binary, role.case_id, role.policy, role.starts, workers, seed,
                role.maximum_evaluations, role.barrier_phases, temporary,
            )
            try:
                subprocess.run(arguments, check=True)
                payload = json.loads(temporary.read_text())
                if payload["maximum_constraint_violation"] >= 0.0:
                    raise RuntimeError("formal run returned an infeasible layout")
                payload["formal_identity"] = expected_identity
                payload["role_id"] = role.role_id
                payload["repeat_index"] = repeat
                atomic_json(destination, payload)
                temporary.unlink(missing_ok=True)
                successes += 1
                physical_fes += int(payload["physical_layout_evaluations"])
            except Exception as error:  # campaign must preserve all failures
                temporary.unlink(missing_ok=True)
                failures.append({
                    "run_id": run_id,
                    "role_id": role.role_id,
                    "error": repr(error),
                    "command": arguments,
                })
            atomic_json(manifest_path, {
                "schema_version": 1,
                "paper_id": "T08",
                "source_commit": source_commit,
                "binary_sha256": binary_digest,
                "status": "running",
                "paper_roles": len(role_list),
                "expected_runs": sum(item.repeats for item in role_list),
                "successful_runs": successes,
                "failed_runs": len(failures),
                "physical_layout_evaluations": physical_fes,
                "total_workers": workers,
                "formal_budget_scale": budget_scale,
                "elapsed_seconds": time.time() - start_time,
                "failures": failures,
            })

    summary = {
        "schema_version": 1,
        "paper_id": "T08",
        "source_commit": source_commit,
        "binary_sha256": binary_digest,
        "status": "pass" if not failures else "fail",
        "paper_roles": len(role_list),
        "expected_runs": sum(item.repeats for item in role_list),
        "successful_runs": successes,
        "failed_runs": len(failures),
        "physical_layout_evaluations": physical_fes,
        "total_workers": workers,
        "formal_budget_scale": budget_scale,
        "elapsed_seconds": time.time() - start_time,
        "failures": failures,
    }
    atomic_json(manifest_path, summary)
    if failures:
        raise RuntimeError(f"T08 formal campaign had {len(failures)} failures")
    return summary


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--source-commit", required=True)
    parser.add_argument("--total-workers", type=int, required=True)
    parser.add_argument("--stage", choices=("h6", "formal", "all"), default="all")
    parser.add_argument("--base-seed", type=int, default=201606101)
    parser.add_argument("--formal-budget-scale", type=float, default=1.0,
                        help="development smoke only; Waffle formal runs use 1.0")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)

    h6 = None
    formal = None
    if args.stage in ("h6", "all"):
        h6 = run_h6(
            args.binary.resolve(), args.output, args.source_commit,
            args.total_workers,
        )
    if args.stage in ("formal", "all"):
        formal = run_formal(
            args.binary.resolve(), args.output, args.source_commit,
            args.total_workers, args.base_seed, args.formal_budget_scale,
        )
    print(json.dumps({"h6": h6, "formal": formal}, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
