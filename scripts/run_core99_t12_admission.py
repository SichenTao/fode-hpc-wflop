#!/usr/bin/env python3
"""Run the four complete T12 paper methods for H6 admission.

WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T12 all-core H6 admission runner
Paper DOI: 10.1016/j.renene.2018.03.052
Public source: https://github.com/d9w/WindFLO revision
9e85a67bb2ca019768ea51dd0b634a46c8406ba2, MIT license
Missing/conflicts and Reconstruction: hpc/core99_cpp/include/core99/windflo_t12.hpp
Method/problem semantic IDs: t12_four_competition_methods_v1;
t12_windflo_2015_five_scenarios_v1
Controlling contract: shared/contracts/core99_t12_windflo_2015.json
Claim boundary: platform admission runner, not author runtime
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
"""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from datetime import UTC, datetime
from pathlib import Path


ALGORITHMS = (
    "t12_goldman_lattice",
    "t12_cmaes_geometric",
    "t12_sshh",
    "t12_3s_mde",
)


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def valid(
    path: Path,
    algorithm: str,
    scenario: int,
    seed: int,
    workers: int,
    physical_fes: int,
) -> bool:
    if not path.is_file():
        return False
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return False
    return (
        value.get("algorithm_id") == algorithm
        and value.get("problem_id") == f"t12_windflo_s{scenario}"
        and value.get("seed") == seed
        and value.get("requested_workers") == workers
        and value.get("physical_fes_limit") == physical_fes
        and value.get("physical_fes", 0) > 0
        and value.get("best_constraint_violation_m", 1.0) <= 1.0e-8
        and bool(value.get("scientific_hash"))
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output-root", type=Path, required=True)
    parser.add_argument("--scenario", type=int, default=1)
    parser.add_argument("--seed", type=int, default=20260731)
    parser.add_argument("--workers", type=int, default=20)
    parser.add_argument(
        "--three-stage-workers",
        type=int,
        default=2,
        help=(
            "workers for 3s-MDE; its 5000-step stateful surrogate paths "
            "are throughput-scaled across independent formal runs"
        ),
    )
    parser.add_argument("--physical-fes", type=int, default=2000)
    parser.add_argument("--source-commit", required=True)
    arguments = parser.parse_args()
    arguments.output_root.mkdir(parents=True, exist_ok=True)
    records = []
    for algorithm in ALGORITHMS:
        selected_workers = (
            arguments.three_stage_workers
            if algorithm == "t12_3s_mde"
            else arguments.workers
        )
        output = arguments.output_root / (
            f"{algorithm}-w{selected_workers}.json"
        )
        reused = valid(
            output,
            algorithm,
            arguments.scenario,
            arguments.seed,
            selected_workers,
            arguments.physical_fes,
        )
        if not reused:
            subprocess.run(
                [
                    str(arguments.binary),
                    "--scenario",
                    str(arguments.scenario),
                    "--algorithm",
                    algorithm,
                    "--seed",
                    str(arguments.seed),
                    "--physical-fes-limit",
                    str(arguments.physical_fes),
                    "--workers",
                    str(selected_workers),
                    "--output",
                    str(output),
                ],
                check=True,
            )
        value = json.loads(output.read_text(encoding="utf-8"))
        records.append(
            {
                "algorithm_id": algorithm,
                "result": output.name,
                "result_sha256": sha256(output),
                "reused": reused,
                "physical_fes": value["physical_fes"],
                "best_energy_cost": value["best_energy_cost"],
                "best_constraint_violation_m": (
                    value["best_constraint_violation_m"]
                ),
                "observed_workers": value["observed_workers"],
                "evaluator_seconds": value["evaluator_seconds"],
                "algorithm_seconds": value["algorithm_seconds"],
                "end_to_end_seconds": value["end_to_end_seconds"],
                "scientific_hash": value["scientific_hash"],
            }
        )
        print(
            f"t12_admission completed={len(records)}/{len(ALGORITHMS)} "
            f"algorithm={algorithm} wall={value['end_to_end_seconds']:.6f}",
            flush=True,
        )
    manifest = {
        "schema_version": 1,
        "campaign_id": "core99_t12_s1_all_methods_h6_v1",
        "generated_at_utc": datetime.now(UTC).isoformat(),
        "source_commit": arguments.source_commit,
        "paper_doi": "10.1016/j.renene.2018.03.052",
        "problem_semantic_id": "t12_windflo_2015_five_scenarios_v1",
        "method_semantic_id": "t12_four_competition_methods_v1",
        "scenario": arguments.scenario,
        "physical_fes_limit": arguments.physical_fes,
        "workers": arguments.workers,
        "execution_policy": {
            "t12_goldman_lattice": {
                "workers_per_optimization": arguments.workers,
                "parallelism": "candidate_by_direction"
            },
            "t12_cmaes_geometric": {
                "workers_per_optimization": arguments.workers,
                "parallelism": "offspring_by_direction"
            },
            "t12_sshh": {
                "workers_per_optimization": arguments.workers,
                "parallelism": "direction"
            },
            "t12_3s_mde": {
                "workers_per_optimization": arguments.three_stage_workers,
                "parallelism": (
                    "stateful trajectories within run; independent formal "
                    "runs provide machine-level throughput"
                )
            }
        },
        "records": records,
        "claim_boundary": (
            "all-core H6 academic declared reproduction; not author runtime"
        ),
    }
    (arguments.output_root / "manifest.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
