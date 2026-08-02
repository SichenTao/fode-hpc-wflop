#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: L0805 H5 semantic, PCE-accuracy and deterministic-HPC gate
Paper/DOI: Shao et al.; 10.1016/J.ENERGY.2025.138820.
Public source, missing assets, conflicts, reconstruction, HPC analysis,
semantic IDs and claim boundary:
hpc/core99_cpp/include/core99/shao_l0805.hpp.
Controlling contract: shared/contracts/core99_l0805_pce_kriging_2025.json.
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
"""

from __future__ import annotations

import argparse
import json
import math
import subprocess


METHOD = "l0805_pce_additive_quadratic_kriging_msp_ei_ga_v1"
PROTOCOL = "l0805_native_30x3_plus_single_iv_v1"
CASES = {
    "l0805_case_i": (8, 9, 80, 343, 50, 30),
    "l0805_case_ii": (16, 13, 160, 567, 50, 30),
    "l0805_case_iii": (32, 17, 320, 839, 50, 30),
    "l0805_case_iv": (8, 9, 160, 272, 8, 1),
}


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
        "pce_seconds", "surrogate_training_seconds",
        "surrogate_inference_seconds", "algorithm_seconds",
        "end_to_end_seconds",
    }
    return {key: value for key, value in payload.items() if key not in ignored}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    args = parser.parse_args()

    roles = execute(args.binary, ["--action", "list-roles"])
    require(roles["protocol_semantic_id"] == PROTOCOL, "L0805 protocol")
    require(roles["required_target_runs"] == 91, "L0805 target-run total")
    observed = {row["case_id"]: row for row in roles["cases"]}
    require(set(observed) == set(CASES), "L0805 paper-case set")
    for case_id, expected in CASES.items():
        row = observed[case_id]
        values = (
            row["turbines"], row["grid_width"],
            row["initial_layout_samples"], row["target_layout_evaluations"],
            row["wind_samples_per_layout"], row["formal_repeats"],
        )
        require(values == expected, f"L0805 native contract {case_id}")

    relative_errors: dict[str, float] = {}
    for case_id in tuple(CASES)[:3]:
        pce = execute(args.binary, [
            "--action", "evaluate", "--case", case_id,
            "--pce-degree", "4", "--seed", "2026080501",
        ])["evaluation"]
        reference = execute(args.binary, [
            "--action", "evaluate", "--case", case_id,
            "--pce-degree", "0", "--seed", "2026080501",
        ])["evaluation"]
        require(pce["physical_wake_simulations"] == 50,
                f"L0805 PCE work {case_id}")
        require(reference["physical_wake_simulations"] == 72 * 22,
                f"L0805 reference work {case_id}")
        error = abs(pce["aep_gwh"] - reference["aep_gwh"]) \
            / reference["aep_gwh"]
        require(error <= 0.05, f"L0805 PCE error {case_id}: {error}")
        relative_errors[case_id] = error

    high = execute(args.binary, [
        "--action", "evaluate", "--case", "l0805_case_iv",
        "--pce-degree", "0",
    ])["evaluation"]
    require(high["physical_wake_simulations"] == 8,
            "L0805 Case IV eight-direction work")

    common = [
        "--action", "optimize", "--case", "l0805_case_i",
        "--seed", "2026080501", "--smoke",
    ]
    serial = execute(args.binary, [*common, "--workers", "1"])
    parallel = execute(args.binary, [*common, "--workers", "20"])
    for payload, workers in ((serial, 1), (parallel, 20)):
        require(payload["method_semantic_id"] == METHOD, "L0805 method")
        require(payload["protocol_semantic_id"] == PROTOCOL, "L0805 protocol")
        require(payload["requested_workers"] == workers, "L0805 workers")
        require(payload["truth_calls"] == 30, "L0805 smoke truth calls")
        require(payload["physical_wake_simulations"] == 30 * 50,
                "L0805 smoke physical work")
        require(payload["best_evaluation"]["aep_gwh"]
                >= payload["initial_best"]["aep_gwh"],
                "L0805 smoke optimization reduced AEP")
        require(all(math.isfinite(value)
                    for value in payload["best_history_gwh"]),
                "L0805 finite history")
    require(science(serial) == science(parallel),
            "L0805 one/all-core science differs")
    require(parallel["observed_workers"] > 1,
            "L0805 optimizer did not engage multiple workers")
    print(json.dumps({
        "status": "pass",
        "method_semantic_id": METHOD,
        "protocol_semantic_id": PROTOCOL,
        "paper_case_contract": "pass",
        "pce_reference_relative_errors": relative_errors,
        "deterministic_hpc": "pass",
        "claim_boundary": (
            "flexible equation/lifecycle reproduction on declared proxies; "
            "not author DAKOTA, FLORIS, OpenFOAM, ADM-CFD or numeric replay"
        ),
    }, sort_keys=True))


if __name__ == "__main__":
    main()
