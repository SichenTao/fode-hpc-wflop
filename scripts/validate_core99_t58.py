#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T58 independent H5 semantic and deterministic-HPC validator
Paper/DOI: Rethore et al.; 10.1002/we.1667
Source facts, omissions and reconstruction boundary:
hpc/core99_cpp/include/core99/rethore_t58.hpp.
Controlling contract: shared/contracts/core99_t58_rethore_topfarm_2014.json
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
"""

from __future__ import annotations

import argparse
import json
import math
import subprocess


PROBLEM = "t58_topfarm_three_case_financial_declared_v1"
PROTOCOL = "t58_native_five_role_single_run_v1"
ROLES = (
    ("2x3", "slp", "t58_slp_declared_v1", 57),
    ("2x3", "sga", "t58_sga_declared_v1", 81),
    ("2x3", "sga-slp", "t58_sga_slp_multifidelity_declared_v1", 138),
    ("stags", "sga-slp", "t58_sga_slp_multifidelity_declared_v1", 226),
    ("middelgrunden", "sga-slp",
     "t58_sga_slp_multifidelity_declared_v1", 250),
)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def execute(binary: str, arguments: list[str]) -> dict:
    completed = subprocess.run(
        [binary, *arguments], check=True, text=True, capture_output=True,
        timeout=15 * 60,
    )
    return json.loads(completed.stdout)


def science(payload: dict) -> dict:
    ignored = {"seconds", "requested_workers", "observed_workers"}
    return {key: value for key, value in payload.items() if key not in ignored}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    args = parser.parse_args()

    listed = execute(args.binary, ["--action", "list-roles"])
    require(listed["protocol_semantic_id"] == PROTOCOL, "T58 protocol ID")
    require(len(listed["native_roles"]) == 5, "T58 native role count")
    require(listed["native_repeats"] == 1, "T58 unreported repeats invented")

    baseline_rows: list[dict] = []
    for case, expected_turbines, expected_efficiency in (
        ("2x3", 6, None), ("stags", 17, 89.4),
        ("middelgrunden", 20, 83.9),
    ):
        common = ["--action", "evaluate-baseline", "--case", case,
                  "--fidelity", "level2"]
        serial = execute(args.binary, [*common, "--workers", "1"])
        parallel = execute(args.binary, [*common, "--workers", "20"])
        require(serial["problem_semantic_id"] == PROBLEM, f"{case}: problem")
        require(serial["turbines"] == expected_turbines, f"{case}: turbines")
        require(serial["fine_wind_states"] == 144, f"{case}: states")
        require(science(serial["evaluation"]) == science(parallel["evaluation"]),
                f"{case}: one/all-core baseline differs")
        require(parallel["evaluation"]["observed_workers"] > 1,
                f"{case}: no multicore evaluator participation")
        require(parallel["evaluation"]["feasible"], f"{case}: baseline infeasible")
        if expected_efficiency is not None:
            actual = parallel["evaluation"]["energy_efficiency_percent"]
            require(abs(actual - expected_efficiency) <= 1.0e-8,
                    f"{case}: report baseline anchor differs")
        no_fatigue = execute(args.binary, [*common, "--workers", "20",
                                           "--fatigue-scale", "0"])
        double_cable = execute(args.binary, [*common, "--workers", "20",
                                             "--cable-scale", "2"])
        base_balance = parallel["evaluation"]["financial_balance_meur"]
        require(no_fatigue["evaluation"]["financial_balance_meur"] > base_balance,
                f"{case}: fatigue branch inactive")
        require(double_cable["evaluation"]["financial_balance_meur"] < base_balance,
                f"{case}: cable branch inactive")
        baseline_rows.append({
            "case": case,
            "turbines": expected_turbines,
            "candidate_count": parallel["candidate_count"],
            "energy_efficiency_percent":
                parallel["evaluation"]["energy_efficiency_percent"],
            "observed_workers": parallel["evaluation"]["observed_workers"],
        })

    role_rows: list[dict] = []
    for case, method, semantic_id, expected_fes in ROLES:
        common = ["--case", case, "--method", method, "--seed", "58077",
                  "--smoke"]
        serial = execute(args.binary, [*common, "--workers", "1"])
        parallel = execute(args.binary, [*common, "--workers", "20"])
        label = f"{case}/{method}"
        require(parallel["problem_semantic_id"] == PROBLEM, f"{label}: problem")
        require(parallel["protocol_semantic_id"] == PROTOCOL, f"{label}: protocol")
        require(parallel["method_semantic_id"] == semantic_id, f"{label}: method")
        require(parallel["physical_fes"] == expected_fes, f"{label}: physical FES")
        require(serial["scientific_hash"] == parallel["scientific_hash"],
                f"{label}: scientific hash differs")
        require(serial["final_layout"] == parallel["final_layout"],
                f"{label}: final layout differs")
        require(science(serial["final_evaluation"])
                == science(parallel["final_evaluation"]),
                f"{label}: final evaluation differs")
        require(parallel["observed_workers"] > 1,
                f"{label}: no multicore optimizer participation")
        if method != "sga":
            require(parallel["final_evaluation"]["feasible"],
                    f"{label}: SLP-final layout infeasible")
        require(math.isfinite(parallel["final_evaluation"]["financial_balance_meur"]),
                f"{label}: non-finite balance")
        role_rows.append({
            "role": label,
            "method_semantic_id": semantic_id,
            "physical_fes": expected_fes,
            "observed_workers": parallel["observed_workers"],
            "scientific_hash": parallel["scientific_hash"],
        })

    print(json.dumps({
        "status": "pass",
        "problem_semantic_id": PROBLEM,
        "protocol_semantic_id": PROTOCOL,
        "baseline_anchors": baseline_rows,
        "paper_native_roles": role_rows,
        "role_count": len(role_rows),
        "claim_boundary": (
            "source-backed flexible academic reproduction; not author HAWTOPT, "
            "HAWC2-DWM database, site arrays, random stream or trajectory replay"
        ),
    }, sort_keys=True))


if __name__ == "__main__":
    main()
