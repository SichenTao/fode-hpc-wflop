#!/usr/bin/env python3
"""H6 and all 196 paper-native T10 roles (1,960 receipts).

WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T10 immutable H6 and full formal campaign
Paper/DOI: 10.1016/j.rser.2016.07.021
Protocol: four target algorithms by four farms, three grid steps, four CHT
labels and ten independent runs, plus four wind-farm-B multi-resolution roles
with ten runs. Every run uses at most one million complete-layout physical FES
and the paper's hypervolume stopping rule under the declared NIS completion.
Full declaration: hpc/core99_cpp/include/core99/rodrigues_t10.hpp
Controlling contract: shared/contracts/core99_t10_rodrigues_2016.json
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
    algorithm: str
    constraint: str
    multi_resolution: bool = False


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


def capture(arguments: list[str]) -> dict:
    completed = subprocess.run(
        arguments, check=True, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE,
    )
    return json.loads(completed.stdout)


def command(
    binary: Path,
    role: Role,
    workers: int,
    seed: int,
    maximum_fes: int,
    output: Path | None = None,
) -> list[str]:
    result = [
        str(binary), "--mode", "optimize", "--case", role.case_id,
        "--algorithm", role.algorithm, "--constraint", role.constraint,
        "--workers", str(workers), "--seed", str(seed),
        "--maximum-fes", str(maximum_fes),
        "--multi-resolution", "true" if role.multi_resolution else "false",
    ]
    if output is not None:
        result.extend(["--output", str(output)])
    return result


def roles() -> list[Role]:
    result: list[Role] = []
    algorithms = ("mogomea", "omogomea", "nsgaii", "c-nsgaii")
    constraints = ("constraint", "penalty", "repair", "resample")
    for farm in "ABCD":
        for step in (8, 4, 2):
            for constraint in constraints:
                for algorithm in algorithms:
                    role_id = f"t10_{farm}_{step}_{constraint}_{algorithm}"
                    result.append(Role(
                        role_id, f"t10_{farm}_{step}", algorithm, constraint
                    ))
    best_cht = {
        "mogomea": "repair",
        "omogomea": "resample",
        "nsgaii": "constraint",
        "c-nsgaii": "constraint",
    }
    for algorithm in algorithms:
        result.append(Role(
            f"t10_B_mr_{algorithm}", "t10_B_8", algorithm,
            best_cht[algorithm], True,
        ))
    assert len(result) == 196
    return result


def run_h6(
    binary: Path, output_root: Path, source_commit: str, workers: int
) -> dict:
    batch: dict[int, list[dict]] = {1: [], workers: []}
    for worker_count in (1, workers):
        for _ in range(3):
            batch[worker_count].append(capture([
                str(binary), "--mode", "evaluate-batch",
                "--case", "t10_D_2", "--constraint", "repair",
                "--seed", "10001", "--batch-size", "128",
                "--workers", str(worker_count),
            ]))
    serial_batch = [item["evaluator_seconds"] for item in batch[1]][1:]
    parallel_batch = [item["evaluator_seconds"] for item in batch[workers]][1:]
    batch_identity = (
        batch[1][-1]["science_hash"] == batch[workers][-1]["science_hash"]
        and batch[1][-1]["energy_sum"] == batch[workers][-1]["energy_sum"]
        and batch[1][-1]["efficiency_sum"]
            == batch[workers][-1]["efficiency_sum"]
    )

    algorithms: dict[str, dict] = {}
    all_identity = batch_identity
    for algorithm in ("mogomea", "omogomea", "nsgaii", "c-nsgaii"):
        role = Role(
            f"h6_{algorithm}", "t10_D_4", algorithm,
            "repair" if algorithm != "omogomea" else "resample",
        )
        observations: dict[int, list[dict]] = {1: [], workers: []}
        for worker_count in (1, workers):
            for _ in range(3):
                observations[worker_count].append(capture(command(
                    binary, role, worker_count, 10002, 2500
                )))
        serial = [item["end_to_end_seconds"] for item in observations[1]][1:]
        parallel = [
            item["end_to_end_seconds"] for item in observations[workers]
        ][1:]
        identity = (
            observations[1][-1]["scientific_hash"]
                == observations[workers][-1]["scientific_hash"]
            and observations[1][-1]["physical_fes"]
                == observations[workers][-1]["physical_fes"]
            and observations[1][-1]["hypervolume"]
                == observations[workers][-1]["hypervolume"]
        )
        all_identity = all_identity and identity
        algorithms[algorithm] = {
            "science_identity": identity,
            "one_worker_seconds_median": statistics.median(serial),
            "all_core_seconds_median": statistics.median(parallel),
            "end_to_end_speedup": statistics.median(serial)
                / statistics.median(parallel),
            "one_worker_receipts": observations[1],
            "all_core_receipts": observations[workers],
        }

    receipt = {
        "schema_version": 1,
        "paper_id": "T10",
        "source_commit": source_commit,
        "binary_sha256": sha256(binary),
        "status": "pass" if all_identity else "fail",
        "total_workers": workers,
        "fixed_evaluator_case": "t10_D_2_batch_128",
        "evaluator_science_identity": batch_identity,
        "one_worker_evaluator_seconds_median": statistics.median(serial_batch),
        "all_core_evaluator_seconds_median": statistics.median(parallel_batch),
        "evaluator_speedup": statistics.median(serial_batch)
            / statistics.median(parallel_batch),
        "observed_evaluator_workers": batch[workers][-1]["observed_workers"],
        "raw_evaluator_receipts": batch,
        "algorithm_h6": algorithms,
    }
    atomic_json(output_root / "h6.json", receipt)
    if not all_identity:
        raise RuntimeError("T10 H6 scientific identity failed")
    return receipt


def run_formal(
    binary: Path,
    output_root: Path,
    source_commit: str,
    workers: int,
    base_seed: int,
    budget_scale: float,
) -> dict:
    if not 0.0 < budget_scale <= 1.0:
        raise ValueError("budget scale must be in (0,1]")
    maximum_fes = max(200, int(round(1_000_000 * budget_scale)))
    role_list = roles()
    runs_root = output_root / "formal-runs"
    runs_root.mkdir(parents=True, exist_ok=True)
    binary_digest = sha256(binary)
    successes = 0
    failures: list[dict] = []
    physical_fes = 0
    start = time.time()
    manifest_path = output_root / "formal-manifest.json"

    for role_index, role in enumerate(role_list):
        for repeat in range(10):
            seed = base_seed + 10000 * role_index + repeat
            run_id = f"{role.role_id}__r{repeat:02d}"
            destination = runs_root / f"{run_id}.json"
            identity = {
                "source_commit": source_commit,
                "binary_sha256": binary_digest,
                "role_id": role.role_id,
                "seed": seed,
                "workers": workers,
                "maximum_physical_fes": maximum_fes,
            }
            if destination.exists():
                try:
                    previous = json.loads(destination.read_text())
                    if previous.get("formal_identity") == identity:
                        successes += 1
                        physical_fes += int(previous["physical_fes"])
                        continue
                except (json.JSONDecodeError, KeyError, TypeError):
                    pass
            temporary = destination.with_suffix(".json.partial")
            try:
                subprocess.run(
                    command(
                        binary, role, workers, seed, maximum_fes, temporary
                    ),
                    check=True, text=True,
                    stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
                )
                payload = json.loads(temporary.read_text())
                if not payload.get("archive") or not all(
                    point.get("occupancy_words")
                    for point in payload["archive"]
                ):
                    raise RuntimeError("T10 formal receipt omits Pareto layouts")
                payload["formal_identity"] = identity
                payload["role_id"] = role.role_id
                payload["repeat"] = repeat
                payload["multi_resolution"] = role.multi_resolution
                atomic_json(destination, payload)
                temporary.unlink(missing_ok=True)
                successes += 1
                physical_fes += int(payload["physical_fes"])
            except Exception as error:  # preserve receipt and continue campaign
                failures.append({
                    "run_id": run_id,
                    "error": str(error),
                })
                temporary.unlink(missing_ok=True)
            manifest = {
                "schema_version": 1,
                "paper_id": "T10",
                "source_commit": source_commit,
                "binary_sha256": binary_digest,
                "status": "running",
                "role_count": len(role_list),
                "expected_receipts": len(role_list) * 10,
                "successes": successes,
                "failures": failures,
                "physical_fes": physical_fes,
                "budget_scale": budget_scale,
                "maximum_physical_fes_per_run": maximum_fes,
                "elapsed_seconds": time.time() - start,
            }
            atomic_json(manifest_path, manifest)

    final = json.loads(manifest_path.read_text())
    final["status"] = "pass" if not failures and successes == 1960 else "fail"
    final["elapsed_seconds"] = time.time() - start
    atomic_json(manifest_path, final)
    if final["status"] != "pass":
        raise RuntimeError(
            f"T10 formal campaign incomplete: success={successes} "
            f"failures={len(failures)}"
        )
    return final


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--source-commit", required=True)
    parser.add_argument("--total-workers", type=int, required=True)
    parser.add_argument("--base-seed", type=int, default=201607021)
    parser.add_argument("--budget-scale", type=float, default=1.0)
    parser.add_argument("--stage", choices=("h6", "formal", "all"), default="all")
    args = parser.parse_args()
    binary = args.binary.resolve()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    if args.stage in ("h6", "all"):
        run_h6(binary, output, args.source_commit, args.total_workers)
    if args.stage in ("formal", "all"):
        run_formal(
            binary, output, args.source_commit, args.total_workers,
            args.base_seed, args.budget_scale,
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
