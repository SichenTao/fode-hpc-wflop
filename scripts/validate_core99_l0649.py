#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: L0649 H5 source-oracle, semantic and deterministic-HPC gate
Paper/DOI: LoCascio et al.; 10.1002/WE.2954.
Public source: paper-linked unlicensed revision dcb729f is an oracle only.
Missing and Reconstruction: SNOPT replacement and random-grid conflict are
declared in hpc/core99_cpp/include/core99/locascio_l0649.hpp.
Semantic IDs: l0649_flowers_aep_analytic_gradient_projected_lbfgs_v1,
l0649_wr7_nine_turbine_14d_square_v1 and
l0649_native_single_optimization_plus_n500_h6_v1.
Claim boundary: source-oracled flexible academic reproduction; full boundary
is recorded in the source header and controlling contract.
Controlling contract: shared/contracts/core99_l0649_flowers_aep_2024.json.
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
"""

from __future__ import annotations

import argparse
import json
import math
import subprocess


PROBLEM = "l0649_wr7_nine_turbine_14d_square_v1"
METHOD = "l0649_flowers_aep_analytic_gradient_projected_lbfgs_v1"
PROTOCOL = "l0649_native_single_optimization_plus_n500_h6_v1"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def execute(binary: str, arguments: list[str]) -> dict:
    completed = subprocess.run(
        [binary, *arguments], check=True, text=True, capture_output=True,
        timeout=30 * 60,
    )
    return json.loads(completed.stdout)


def science(payload: dict) -> dict:
    ignored = {
        "requested_workers", "observed_workers", "evaluator_seconds",
        "algorithm_seconds", "end_to_end_seconds",
    }
    result = {key: value for key, value in payload.items() if key not in ignored}
    for field in ("initial_evaluation", "final_evaluation"):
        result[field] = {
            key: value for key, value in result[field].items()
            if key not in {"requested_workers", "observed_workers", "seconds"}
        }
    return result


def validate_run(payload: dict, workers: int) -> None:
    require(payload["corpus_id"] == "L0649", "L0649 corpus")
    require(payload["problem_semantic_id"] == PROBLEM, "L0649 problem")
    require(payload["method_semantic_id"] == METHOD, "L0649 method")
    require(payload["protocol_semantic_id"] == PROTOCOL, "L0649 protocol")
    require(payload["requested_workers"] == workers, "L0649 workers")
    require(payload["paper_role"] == "wr7_nine_turbine_flowers_opt",
            "L0649 role")
    require(len(payload["final_layout"]) == 9, "L0649 layout count")
    for point in payload["final_layout"]:
        require(0.0 <= point["x_m"] <= 1764.0, "L0649 x boundary")
        require(0.0 <= point["y_m"] <= 1764.0, "L0649 y boundary")
    initial = payload["initial_evaluation"]["aep_wh"]
    final = payload["final_evaluation"]["aep_wh"]
    require(math.isfinite(initial) and math.isfinite(final), "L0649 finite AEP")
    require(abs(initial - 120870064988.85013) / initial < 2e-14,
            "L0649 author-source AEP oracle")
    require(final >= initial, "L0649 optimization reduced AEP")
    require(13.7 <= payload["objective_gain_percent"] <= 14.0,
            "L0649 paper objective-gain neighborhood")
    require(1 <= payload["iterations"] <= 200, "L0649 iterations")
    require(payload["history"][-1]["projected_gradient_inf"] <= 1.0e-3,
            "L0649 projected optimality tolerance")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    args = parser.parse_args()
    roles = execute(args.binary, ["--action", "list-roles"])
    require(roles["protocol_semantic_id"] == PROTOCOL, "L0649 roles protocol")
    require(roles["native_roles"] == ["wr7_nine_turbine_flowers_opt"],
            "L0649 native role set")
    require(roles["native_repeats"] == 1, "L0649 native repeats")

    serial = execute(args.binary, ["--action", "optimize", "--workers", "1"])
    parallel = execute(args.binary, [
        "--action", "optimize", "--workers", "20",
    ])
    validate_run(serial, 1)
    validate_run(parallel, 20)
    require(science(serial) == science(parallel),
            "L0649 one/all-core science differs")
    require(parallel["observed_workers"] > 1,
            "L0649 optimizer did not engage multiple workers")

    scale_serial = execute(args.binary, [
        "--action", "evaluate-scale", "--turbines", "500", "--workers", "1",
    ])
    scale_parallel = execute(args.binary, [
        "--action", "evaluate-scale", "--turbines", "500", "--workers", "20",
    ])
    one = scale_serial["evaluation"]
    all_workers = scale_parallel["evaluation"]
    require(one["aep_wh"] == all_workers["aep_wh"],
            "L0649 N500 one/all-core AEP differs")
    require(one["ordered_pair_terms"] == 2495000,
            "L0649 N500 M10 pair work differs")
    require(all_workers["observed_workers"] > 1,
            "L0649 N500 evaluation did not engage multiple workers")
    print(json.dumps({
        "status": "pass",
        "problem_semantic_id": PROBLEM,
        "method_semantic_id": METHOD,
        "protocol_semantic_id": PROTOCOL,
        "author_source_initial_aep_oracle": "pass",
        "paper_objective_gain_neighborhood": "pass",
        "deterministic_hpc": "pass",
        "claim_boundary": (
            "source-oracled flexible reproduction; not author code, SNOPT "
            "trajectory, FLORIS baselines, randomized corpus or timing replay"
        ),
    }, sort_keys=True))


if __name__ == "__main__":
    main()
