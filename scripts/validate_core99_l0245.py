#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: L0245 H5 semantic, numerical and deterministic-HPC gate.
Paper/DOI, public assets, missing data, conflicts, reconstruction decisions,
semantic IDs, HPC design and claim boundary:
hpc/core99_cpp/include/core99/padron_l0245.hpp.
Controlling contract: shared/contracts/core99_l0245_padron_2019.json.
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
"""

from __future__ import annotations

import argparse
import json
import math
import subprocess


METHOD = "l0245_pcr_cv_gradient_slsqp_declared_v1"
PROBLEM = "l0245_amalia60_two_uncertainty_floris_declared_v1"
PROTOCOL = "l0245_four_layout_convergence_three_start_10set_v1"
LAYOUTS = ["grid", "amalia", "optimized", "random"]
METHODS = ["pcr_coarse", "pcr_fine", "rectangle_coarse", "rectangle_fine"]


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def execute(binary: str, arguments: list[str], timeout: int = 30 * 60) -> dict:
    completed = subprocess.run(
        [binary, *arguments], check=True, text=True, capture_output=True,
        timeout=timeout,
    )
    return json.loads(completed.stdout)


def profile_science(payload: dict) -> dict:
    ignored = {"requested_workers", "observed_workers", "seconds"}
    return {key: value for key, value in payload.items() if key not in ignored}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    parser.add_argument("--data", required=True)
    args = parser.parse_args()
    base = ["--data", args.data]

    roles = execute(args.binary, ["--action", "list-roles"])
    require(roles["protocol_semantic_id"] == PROTOCOL, "L0245 protocol")
    require(roles["layouts"] == LAYOUTS, "L0245 layout roles")
    require(roles["methods"] == METHODS, "L0245 method roles")
    require(roles["reference_method"] == "monte_carlo_reference",
            "L0245 reference method")
    require(roles["sample_counts"] == [231, 630, 225, 625, 200000],
            "L0245 sample counts")
    require(roles["sample_sets"] == 10, "L0245 sample sets")
    require(roles["required_optimization_runs"] == 120,
            "L0245 optimization role count")

    fixed = {}
    for layout in LAYOUTS:
        row = execute(args.binary, [
            "--action", "evaluate", *base, "--layout", layout,
            "--method", "rectangle_fine", "--seed", "2019024501",
            "--workers", "20",
        ])
        require(row["method_semantic_id"] == METHOD, "L0245 method")
        require(row["problem_semantic_id"] == PROBLEM, "L0245 problem")
        evaluation = row["evaluation"]
        require(evaluation["physical_wake_simulations"] == 625,
                "L0245 rectangle-fine state count")
        require(evaluation["requested_workers"] == 20
                and evaluation["observed_workers"] > 1,
                "L0245 fixed-layout all-core participation")
        require(math.isfinite(evaluation["aep_gwh"])
                and 500.0 < evaluation["aep_gwh"] < 2500.0,
                "L0245 fixed-layout AEP range")
        fixed[layout] = evaluation["aep_gwh"]

    common = [
        "--action", "profile", *base, "--layout", "amalia",
        "--method", "pcr_coarse", "--seed", "2019024501",
        "--repeats", "2",
    ]
    serial = execute(args.binary, [*common, "--workers", "1"])
    parallel = execute(args.binary, [*common, "--workers", "20"])
    require(profile_science(serial) == profile_science(parallel),
            "L0245 one/all-core science")
    require(parallel["observed_workers"] > 1,
            "L0245 profile all-core participation")

    pcr = execute(args.binary, [
        "--action", "evaluate", *base, "--layout", "amalia",
        "--method", "pcr_fine", "--seed", "2019024501",
        "--workers", "20",
    ])["evaluation"]
    rectangle = execute(args.binary, [
        "--action", "evaluate", *base, "--layout", "amalia",
        "--method", "rectangle_fine", "--seed", "2019024501",
        "--workers", "20",
    ])["evaluation"]
    reference = execute(args.binary, [
        "--action", "evaluate", *base, "--layout", "amalia",
        "--method", "monte_carlo_reference", "--seed", "2019024501",
        "--workers", "20",
    ], timeout=60 * 60)["evaluation"]
    require(1 <= pcr["selected_polynomial_degree"] <= 19,
            "L0245 selected polynomial degree")
    require(pcr["physical_wake_simulations"] == 630,
            "L0245 PC-R fine count")
    require(reference["physical_wake_simulations"] == 200000,
            "L0245 Monte-Carlo count")
    for name, row in (("PC-R", pcr), ("rectangle", rectangle)):
        relative_error = abs(row["aep_gwh"] - reference["aep_gwh"]) \
            / reference["aep_gwh"]
        require(relative_error < 0.05,
                f"L0245 {name} is not close to MC reference")

    optimized = execute(args.binary, [
        "--action", "optimize", *base, "--layout", "amalia",
        "--method", "pcr_coarse", "--seed", "2019024501",
        "--workers", "20", "--evaluations", "12", "--smoke",
    ], timeout=60 * 60)
    require(optimized["method_semantic_id"] == METHOD, "L0245 method")
    require(optimized["protocol_semantic_id"] == PROTOCOL,
            "L0245 protocol")
    require(optimized["observed_workers"] > 1,
            "L0245 optimizer all-core participation")
    require(optimized["final_evaluation"]["feasible"],
            "L0245 final feasibility")
    require(optimized["final_evaluation"]["aep_gwh"] + 1e-9
            >= optimized["initial_evaluation"]["aep_gwh"],
            "L0245 optimizer reduced AEP")
    require(len(optimized["final_layout_m"]) == 60,
            "L0245 final layout size")

    print(json.dumps({
        "status": "pass",
        "method_semantic_id": METHOD,
        "problem_semantic_id": PROBLEM,
        "protocol_semantic_id": PROTOCOL,
        "fixed_layout_rectangle_fine_aep_gwh": fixed,
        "pcr_fine_aep_gwh": pcr["aep_gwh"],
        "rectangle_fine_aep_gwh": rectangle["aep_gwh"],
        "monte_carlo_reference_aep_gwh": reference["aep_gwh"],
        "deterministic_hpc": "pass",
        "automatic_derivative_finite_difference": "covered_by_cpp_test",
        "claim_boundary": (
            "source-backed flexible academic reproduction; not author "
            "DAKOTA, SNOPT, random-state, numeric-table or timing replay"
        ),
    }, sort_keys=True))


if __name__ == "__main__":
    main()
