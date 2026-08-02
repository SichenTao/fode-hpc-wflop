#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T14 Waffle all-core H6 admission runner
Paper DOI: 10.5194/wes-4-663-2019
Public source and reconstruction boundary:
hpc/core99_cpp/include/core99/stanley_t14.hpp
Missing/conflicts: proprietary SNOPT is replaced by the declared optimizer;
this runner admits the reconstruction and never labels it author SNOPT
Reconstruction: run all three intrinsic paper representations on the central
paper case with full declared budgets, immutable source commit, raw receipts,
and wall/CPU resource observations
Method/problem semantic IDs: t14_boundary_grid_parameterization_v1;
t14_stanley_2019_seven_unique_cases_v1
Controlling contract: shared/contracts/core99_t14_stanley_2019.json
Claim boundary: H6 engineering/scientific admission, not formal 100-start data
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


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output-root", type=Path, required=True)
    parser.add_argument("--workers", type=int, required=True)
    parser.add_argument("--seed", type=int, default=20260731)
    parser.add_argument("--source-commit", required=True)
    parser.add_argument(
        "--case",
        default="t14_spacing4_amalia_north_island",
    )
    arguments = parser.parse_args()
    arguments.output_root.mkdir(parents=True, exist_ok=True)
    manifest: dict[str, Any] = {
        "admission": "T14 all-core H6",
        "case": arguments.case,
        "method_semantic_id": "t14_boundary_grid_parameterization_v1",
        "problem_semantic_id": "t14_stanley_2019_seven_unique_cases_v1",
        "seed": arguments.seed,
        "source_commit": arguments.source_commit,
        "status": "running",
        "workers": arguments.workers,
        "runs": {},
    }
    budgets = {
        "t14_direct": 160,
        "t14_grid": 240,
        "t14_boundary_grid": 240,
    }
    for algorithm, budget in budgets.items():
        output = arguments.output_root / f"{algorithm}-w{arguments.workers}.json"
        command = [
            str(arguments.binary),
            "--algorithm",
            algorithm,
            "--case",
            arguments.case,
            "--seed",
            str(arguments.seed),
            "--physical-fes-limit",
            str(budget),
            "--workers",
            str(arguments.workers),
            "--output",
            str(output),
        ]
        start = time.perf_counter()
        completed = subprocess.run(
            command,
            text=True,
            capture_output=True,
            check=False,
        )
        wall = time.perf_counter() - start
        stdout = output.with_suffix(".stdout")
        stderr = output.with_suffix(".stderr")
        stdout.write_text(completed.stdout, encoding="utf-8")
        stderr.write_text(completed.stderr, encoding="utf-8")
        if completed.returncode != 0:
            manifest["status"] = "fail"
            manifest["runs"][algorithm] = {
                "returncode": completed.returncode,
                "wall_seconds": wall,
            }
            (arguments.output_root / "manifest.json").write_text(
                json.dumps(manifest, indent=2, sort_keys=True) + "\n",
                encoding="utf-8",
            )
            raise SystemExit(completed.returncode)
        receipt = json.loads(output.read_text(encoding="utf-8"))
        manifest["runs"][algorithm] = {
            "best_constraint_violation_m":
                receipt["best_constraint_violation_m"],
            "best_final_aep_gwh": receipt["best_final_aep_gwh"],
            "best_optimization_aep_gwh":
                receipt["best_optimization_aep_gwh"],
            "end_to_end_seconds": receipt["end_to_end_seconds"],
            "observed_workers": receipt["observed_workers"],
            "physical_fes": receipt["physical_fes"],
            "returncode": completed.returncode,
            "scientific_hash": receipt["scientific_hash"],
            "wall_seconds": wall,
        }
        if (
            receipt["best_constraint_violation_m"] > 1.0e-5
            or receipt["observed_workers"] != arguments.workers
            or receipt["physical_fes"] != budget
        ):
            manifest["status"] = "fail"
            (arguments.output_root / "manifest.json").write_text(
                json.dumps(manifest, indent=2, sort_keys=True) + "\n",
                encoding="utf-8",
            )
            raise SystemExit(
                f"T14 H6 admission failed for {algorithm}: "
                "feasibility/worker/FES gate"
            )
        print(
            "t14_admission"
            f" completed={len(manifest['runs'])}/{len(budgets)}"
            f" algorithm={algorithm} wall={wall:.6f}",
            flush=True,
        )
    manifest["status"] = "pass"
    (arguments.output_root / "manifest.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )


if __name__ == "__main__":
    main()
