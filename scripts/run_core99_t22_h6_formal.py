#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T22 Waffle all-core H6 and paper-native DEBO runner
Paper/DOI: A Comparison of Eight Optimization Methods Applied to a Wind Farm
Layout Optimization Problem; 10.5194/wes-8-865-2023
Public source: https://github.com/jaredthomas68/thomas2022-8-opt-algs-wflop
at revision 41d7290b8cc9bf3d90b25d844312f4790037806d; archive
10.5281/zenodo.7125349
Missing/conflicts/completion:
hpc/core99_cpp/include/core99/debo_t22.hpp
Formal protocol: the paper reports that DEBO required one run because it has
little randomness; this runner therefore executes one full paper-termination
DEBO run after a small fixed-layout one-worker/all-core timing comparison
Claim boundary: academic declared reconstruction, not author-source or
author-exact numerical DEBO replay
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
"""

from __future__ import annotations

import argparse
import json
import statistics
import subprocess
from pathlib import Path
from typing import Any


def run_json(command: list[str]) -> dict[str, Any]:
    completed = subprocess.run(
        command,
        check=True,
        text=True,
        capture_output=True,
    )
    return json.loads(completed.stdout)


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(
        json.dumps(payload, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    temporary.replace(path)


def fixed_receipt(
    binary: Path,
    workers: int,
    repeats: int,
) -> dict[str, Any]:
    return run_json(
        [
            str(binary),
            "--author-layout",
            "debo",
            "--workers",
            str(workers),
            "--evaluation-repeats",
            str(repeats),
        ]
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output-root", type=Path, required=True)
    parser.add_argument("--source-commit", required=True)
    parser.add_argument("--workers", type=int, default=20)
    parser.add_argument("--timing-repeats", type=int, default=101)
    parser.add_argument("--timing-observations", type=int, default=5)
    parser.add_argument("--seed", type=int, default=20260731)
    parser.add_argument("--run-formal", action="store_true")
    arguments = parser.parse_args()

    if (
        arguments.workers <= 1
        or arguments.timing_repeats <= 0
        or arguments.timing_observations < 3
    ):
        raise SystemExit("invalid T22 H6 timing contract")
    arguments.output_root.mkdir(parents=True, exist_ok=True)

    one_worker = [
        fixed_receipt(
            arguments.binary,
            1,
            arguments.timing_repeats,
        )
        for _ in range(arguments.timing_observations)
    ]
    all_core = [
        fixed_receipt(
            arguments.binary,
            arguments.workers,
            arguments.timing_repeats,
        )
        for _ in range(arguments.timing_observations)
    ]
    reference = one_worker[0]
    for receipt in one_worker + all_core:
        if (
            receipt["aep_mwh"] != reference["aep_mwh"]
            or receipt["wake_loss_fraction"]
            != reference["wake_loss_fraction"]
            or receipt["constraint_violation_m"]
            != reference["constraint_violation_m"]
        ):
            raise SystemExit("T22 H6 scientific identity mismatch")
    if any(
        receipt["observed_workers"] != arguments.workers
        for receipt in all_core
    ):
        raise SystemExit("T22 H6 did not exercise every requested worker")

    one_seconds = statistics.median(
        receipt["seconds_per_evaluation"] for receipt in one_worker
    )
    all_seconds = statistics.median(
        receipt["seconds_per_evaluation"] for receipt in all_core
    )
    manifest: dict[str, Any] = {
        "schema_version": 1,
        "campaign": "T22 all-core H6 and paper-native formal",
        "paper_doi": "10.5194/wes-8-865-2023",
        "method_semantic_id": "t22_debo_paper_reconstruction_v1",
        "problem_semantic_id": "t22_iea37_cs4_gaussian_aep_v1",
        "source_commit": arguments.source_commit,
        "workers": arguments.workers,
        "timing_repeats_per_observation": arguments.timing_repeats,
        "timing_observations": arguments.timing_observations,
        "fixed_layout_h6": {
            "layout": "paper_debo",
            "aep_mwh": reference["aep_mwh"],
            "constraint_violation_m":
                reference["constraint_violation_m"],
            "one_worker_median_seconds_per_evaluation": one_seconds,
            "all_core_median_seconds_per_evaluation": all_seconds,
            "evaluator_speedup": one_seconds / all_seconds,
            "observed_workers": arguments.workers,
            "scientific_identity": "exact",
            "status": "pass",
        },
        "formal": {
            "paper_required_runs": 1,
            "status": "not_requested",
        },
        "status": "h6_pass_formal_not_requested",
    }
    write_json(arguments.output_root / "manifest.json", manifest)

    if arguments.run_formal:
        output = arguments.output_root / "formal-seed-20260731.json"
        if output.exists():
            formal = json.loads(output.read_text(encoding="utf-8"))
        else:
            completed = subprocess.run(
                [
                    str(arguments.binary),
                    "--seed",
                    str(arguments.seed),
                    "--workers",
                    str(arguments.workers),
                    "--physical-fes-limit",
                    "0",
                    "--output",
                    str(output),
                ],
                check=False,
            )
            if completed.returncode != 0:
                raise SystemExit(completed.returncode)
            formal = json.loads(output.read_text(encoding="utf-8"))
        if (
            formal["algorithm_id"] != "t22_debo"
            or not formal["paper_termination_reached"]
            or formal["observed_workers"] != arguments.workers
            or formal["best_constraint_violation_m"] > 1.0e-5
            or formal["physical_fes"] <= 0
        ):
            raise SystemExit("T22 paper-native formal admission failed")
        manifest["formal"] = {
            "paper_required_runs": 1,
            "completed_runs": 1,
            "receipt": str(output),
            "seed": formal["seed"],
            "physical_fes": formal["physical_fes"],
            "best_aep_mwh": formal["best_aep_mwh"],
            "best_constraint_violation_m":
                formal["best_constraint_violation_m"],
            "observed_workers": formal["observed_workers"],
            "end_to_end_seconds": formal["end_to_end_seconds"],
            "scientific_hash": formal["scientific_hash"],
            "status": "pass",
        }
        manifest["status"] = "pass"
        write_json(arguments.output_root / "manifest.json", manifest)
    print(
        "t22_h6_formal"
        f" status={manifest['status']}"
        f" evaluator_speedup={manifest['fixed_layout_h6']['evaluator_speedup']:.6f}"
        f" formal={manifest['formal']['status']}",
        flush=True,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
