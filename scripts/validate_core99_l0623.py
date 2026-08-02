#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: independent L0623 H5 semantic and numerical validator
Paper/DOI/source/missing/reconstruction/claim:
hpc/core99_cpp/include/core99/wang_l0623.hpp
Controlling contract:
shared/contracts/core99_l0623_wang_cfd_kriging_2024.json
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
"""
from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
CASES = (
    "l0623_case1_flat_single",
    "l0623_case2_flat_windrose",
    "l0623_case3_hill_windrose",
)
PAPER_BASELINE_AEP = (141.324, 97.287, 135.732)
PAPER_TRUTH_CALLS = (437, 400, 399)
BASELINE = "0,8,22,36,44,58,72,80"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def call(binary: str, arguments: list[str]) -> dict:
    completed = subprocess.run(
        [binary, *arguments],
        text=True,
        capture_output=True,
        check=True,
    )
    return json.loads(completed.stdout)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    args = parser.parse_args()

    cases = call(args.binary, ["--mode", "list-cases"])["paper_case_ids"]
    require(cases == list(CASES), "L0623 paper case registry mismatch")
    inspections = []
    baseline_values = []
    errors = []
    for index, case_id in enumerate(CASES):
        inspection = call(
            args.binary, ["--mode", "inspect", "--case", case_id]
        )
        inspections.append(inspection)
        require(
            inspection["candidate_count"] == 81
            and inspection["turbine_count"] == 8,
            f"{case_id}: discrete problem mismatch",
        )
        require(
            inspection["paper_initial_samples"] == 360
            and inspection["paper_population"] == 50
            and inspection["paper_maximum_ga_generations"] == 1000
            and inspection["paper_truth_calls"] == PAPER_TRUTH_CALLS[index],
            f"{case_id}: paper framework setting mismatch",
        )
        evaluation = call(
            args.binary,
            [
                "--mode", "evaluate",
                "--case", case_id,
                "--indices", BASELINE,
            ],
        )["evaluation"]
        require(
            evaluation["feasible"]
            and math.isfinite(evaluation["aep_gwh"])
            and len(evaluation["turbine_aep_gwh"]) == 8,
            f"{case_id}: source-figure baseline invalid",
        )
        baseline_values.append(evaluation["aep_gwh"])
        errors.append(
            abs(evaluation["aep_gwh"] - PAPER_BASELINE_AEP[index])
            / PAPER_BASELINE_AEP[index]
        )
    require(
        max(errors) < 0.15,
        f"L0623 declared proxy exceeds 15% Table-5 scale boundary: {errors}",
    )
    require(
        inspections[0]["wind_direction_count"] == 1
        and inspections[1]["wind_direction_count"] == 8
        and inspections[2]["wind_direction_count"] == 8
        and not inspections[0]["gaussian_hill"]
        and not inspections[1]["gaussian_hill"]
        and inspections[2]["gaussian_hill"],
        "L0623 wind/terrain case semantics mismatch",
    )

    with tempfile.TemporaryDirectory(prefix="l0623-h5-") as directory:
        outputs = {}
        for workers in (1, 4):
            output = Path(directory) / f"workers-{workers}.json"
            subprocess.run(
                [
                    args.binary,
                    "--mode", "optimize",
                    "--case", CASES[0],
                    "--workers", str(workers),
                    "--initial-samples", "64",
                    "--maximum-truth-calls", "68",
                    "--maximum-ga-generations", "20",
                    "--seed", "623623",
                    "--output", str(output),
                ],
                check=True,
            )
            outputs[workers] = json.loads(output.read_text())
    require(
        outputs[1]["truth_calls"] == 68
        and outputs[4]["truth_calls"] == 68,
        "L0623 truth-call accounting mismatch",
    )
    require(
        outputs[1]["scientific_hash"] == outputs[4]["scientific_hash"],
        "L0623 one/multicore scientific hash mismatch",
    )
    require(
        outputs[4]["observed_workers"] >= 2,
        "L0623 multicore backend was not observed",
    )
    require(
        outputs[4]["surrogate_fes"] > 0,
        "L0623 adaptive Kriging-GA did no surrogate work",
    )
    require(
        outputs[4]["best_evaluation"]["feasible"]
        and outputs[4]["best_evaluation"]["aep_gwh"] + 1.0e-9
            >= outputs[4]["initial_best"]["aep_gwh"],
        "L0623 final design infeasible or regressed",
    )

    receipt = {
        "status": "pass",
        "corpus_id": "L0623",
        "paper_case_count": len(cases),
        "paper_truth_calls": list(PAPER_TRUTH_CALLS),
        "paper_baseline_aep_gwh": list(PAPER_BASELINE_AEP),
        "proxy_baseline_aep_gwh": baseline_values,
        "baseline_relative_errors": errors,
        "smoke_truth_calls": outputs[4]["truth_calls"],
        "smoke_surrogate_fes": outputs[4]["surrogate_fes"],
        "observed_smoke_workers": outputs[4]["observed_workers"],
        "scientific_hash": outputs[4]["scientific_hash"],
        "claim_boundary":
            "paper problem and adaptive Kriging-GA academic reproduction "
            "on a declared ADM/Gaussian truth proxy; not author OpenFOAM "
            "CFD, source, mesh, responses or numerical replay",
    }
    output = ROOT / "evidence/core99/h5/L0623_local_h5.json"
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(
        json.dumps(receipt, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(json.dumps(receipt, sort_keys=True))


if __name__ == "__main__":
    main()
