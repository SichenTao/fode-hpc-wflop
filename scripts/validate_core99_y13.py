#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: Y13 independent H5 semantics and deterministic-HPC gate
Paper/DOI: Du et al.; 10.1109/TSTE.2025.3609006.
Facts, missing assets, corrections and claim boundary:
hpc/core99_cpp/include/core99/du_y13.hpp.
Controlling contract: shared/contracts/core99_y13_du_grid_admm_2026.json.
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
"""

from __future__ import annotations

import argparse
import json
import math
import subprocess


PROBLEM = "y13_four_grid_fg36_declared_v1"
METHOD = "y13_l2box_consensus_admm_highs_declared_v1"
PROTOCOL = "y13_native_four_case_single_run_v1"
CASES = (("6x6", 36, 18), ("10x10", 100, 50),
         ("16x16", 256, 128), ("20x20", 400, 200))


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
        "requested_workers", "observed_workers", "matrix_seconds",
        "subproblem_seconds", "evaluator_seconds", "algorithm_seconds",
        "end_to_end_seconds",
    }
    result = {key: value for key, value in payload.items() if key not in ignored}
    for field in ("initial_evaluation", "final_evaluation"):
        result[field] = {
            key: value for key, value in result[field].items()
            if key not in {"requested_workers", "observed_workers", "seconds"}
        }
    result["iterations"] = [
        {key: value for key, value in row.items()
         if key != "subproblem_seconds"}
        for row in result["iterations"]
    ]
    return result


def validate(payload: dict, case: str, cells: int, turbines: int) -> None:
    require(payload["corpus_id"] == "Y13", f"{case}: corpus")
    require(payload["problem_semantic_id"] == PROBLEM, f"{case}: problem")
    require(payload["method_semantic_id"] == METHOD, f"{case}: method")
    require(payload["protocol_semantic_id"] == PROTOCOL, f"{case}: protocol")
    require(payload["case_id"] == case, f"{case}: case")
    require(payload["cells"] == cells, f"{case}: cells")
    require(payload["turbines"] == turbines, f"{case}: turbines")
    require(payload["wind_scenarios"] == 36, f"{case}: scenarios")
    require(len(payload["selected_cells"]) == turbines, f"{case}: cardinality")
    require(len(set(payload["selected_cells"])) == turbines,
            f"{case}: duplicate cell")
    require(payload["complete_layout_evaluations"] == 2,
            f"{case}: complete-layout evaluations")
    require(payload["scenario_subproblem_solves"]
            == 36 * payload["admm_iterations"], f"{case}: scenario work")
    require(1 <= payload["admm_iterations"] <= 10, f"{case}: iterations")
    require(payload["final_rounding_deviation"] <= 0.05 + 1e-12,
            f"{case}: paper rounding range")
    final = payload["final_evaluation"]
    require(math.isfinite(final["net_aep_gwh"]), f"{case}: finite AEP")
    require(0.0 < final["net_aep_gwh"] <= final["gross_aep_gwh"],
            f"{case}: AEP range")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    args = parser.parse_args()

    roles = execute(args.binary, ["--action", "list-roles"])
    require(roles["protocol_semantic_id"] == PROTOCOL, "Y13 protocol")
    require(len(roles["native_roles"]) == 4, "Y13 role count")
    require(roles["native_repeats"] == 1, "Y13 invented repeats")

    rows = []
    for case, cells, turbines in CASES:
        payload = execute(args.binary, [
            "--case", case, "--workers", "20", "--iterations", "10",
        ])
        validate(payload, case, cells, turbines)
        require(payload["observed_workers"] > 1, f"{case}: no HPC participation")
        rows.append({
            "case": case,
            "cells": cells,
            "turbines": turbines,
            "net_aep_gwh": payload["final_evaluation"]["net_aep_gwh"],
            "rounding_deviation": payload["final_rounding_deviation"],
            "observed_workers": payload["observed_workers"],
            "scientific_hash": payload["scientific_hash"],
        })

    common = ["--case", "6x6", "--iterations", "2", "--smoke"]
    serial = execute(args.binary, [*common, "--workers", "1"])
    parallel = execute(args.binary, [*common, "--workers", "20"])
    validate(serial, "6x6", 36, 18)
    validate(parallel, "6x6", 36, 18)
    require(science(serial) == science(parallel),
            "Y13 one/all-core science differs")
    require(parallel["observed_workers"] > 1,
            "Y13 smoke optimizer did not use multiple workers")

    print(json.dumps({
        "status": "pass",
        "problem_semantic_id": PROBLEM,
        "method_semantic_id": METHOD,
        "protocol_semantic_id": PROTOCOL,
        "paper_native_roles": rows,
        "deterministic_hpc": "pass",
        "claim_boundary": (
            "equation-level flexible reproduction; not author Gurobi code, "
            "Danish data, warm start, trajectory, table identity or timing replay"
        ),
    }, sort_keys=True))


if __name__ == "__main__":
    main()
