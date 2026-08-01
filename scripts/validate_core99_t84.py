#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: T84 independent H5 semantic and deterministic-HPC validator
Paper/DOI: Wake Expansion Continuation: Multi-Modality Reduction in the Wind
Farm Layout Optimization Problem; 10.1002/we.2692
Public source: thomas2021-wec 8ff27d66079591f25619a plus pinned PlantEnergy
and Jensen3D model oracles. Missing solver states, conflicts and reconstruction
resolutions: hpc/core99_cpp/include/core99/thomas_t84.hpp.
Semantic IDs: t84_wec_four_case_author_data_v1 and four t84_* method IDs.
Production backend validated: pure C++ CPU-HPC one/all-core deterministic
directions and populations.
Controlling contract: shared/contracts/core99_t84_thomas_2022.json
Claim boundary: source-backed flexible academic reproduction, not author
solver/environment/random-state numerical replay.
Last evidence-audit date: 2026-08-01
END WFLOP IMPLEMENTATION FACT DECLARATION
"""

from __future__ import annotations

import argparse
import json
import math
import subprocess


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def execute(binary: str, arguments: list[str]) -> dict:
    completed = subprocess.run(
        [binary, *arguments], check=True, text=True, capture_output=True
    )
    return json.loads(completed.stdout)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    parser.add_argument("--data", required=True)
    args = parser.parse_args()

    problems = [
        (1, "bastankhah", 16, 20),
        (2, "bastankhah", 38, 12),
        (2, "jensen", 38, 12),
        (3, "bastankhah", 38, 36),
        (4, "bastankhah", 60, 72),
    ]
    evaluations: list[dict] = []
    # Run-0 initial AEP values from the pinned author result tables. Case 1
    # is deliberately excluded because the paper says 10 m/s while the
    # public run driver/result used the conflicting 8 m/s input file.
    author_initial_aep_gwh = {
        (2, "bastankhah"): 153.3390139744621,
        (2, "jensen"): 170.5522140679032,
        (3, "bastankhah"): 47.76905228281987,
        (4, "bastankhah"): 348.7915500393433,
    }
    for case_id, wake, turbines, wind_states in problems:
        common = [
            "--mode", "evaluate", "--data", args.data,
            "--case", str(case_id), "--start-index", "0",
            "--wake", wake,
            "--turbulence", "hard" if wake == "bastankhah" else "ambient",
            "--gradient",
        ]
        serial = execute(args.binary, [*common, "--workers", "1"])
        parallel = execute(args.binary, [*common, "--workers", "20"])
        left = serial["evaluation"]
        right = parallel["evaluation"]
        require(len(left["directional_power_mw"]) == wind_states,
                f"case {case_id}/{wake}: wind-state count mismatch")
        require(len(left["gradient_gwh_per_m"]) == 2 * turbines,
                f"case {case_id}/{wake}: gradient dimension mismatch")
        require(left["aep_gwh"] == right["aep_gwh"],
                f"case {case_id}/{wake}: one/all-core AEP mismatch")
        require(left["gradient_gwh_per_m"] == right["gradient_gwh_per_m"],
                f"case {case_id}/{wake}: one/all-core gradient mismatch")
        require(right["observed_workers"] > 1,
                f"case {case_id}/{wake}: no multicore participation")
        require(math.isfinite(right["aep_gwh"]) and right["aep_gwh"] > 0.0,
                f"case {case_id}/{wake}: invalid AEP")
        expected = author_initial_aep_gwh.get((case_id, wake))
        relative_error = None
        if expected is not None:
            relative_error = abs(right["aep_gwh"] - expected) / expected
            require(relative_error <= 0.02,
                    f"case {case_id}/{wake}: author run-0 AEP error exceeds 2%")
        evaluations.append({
            "case_id": case_id,
            "wake": wake,
            "aep_gwh": right["aep_gwh"],
            "wake_loss_percent": right["wake_loss_percent"],
            "observed_workers": right["observed_workers"],
            "author_run0_aep_gwh": expected,
            "author_run0_relative_error": relative_error,
        })

    roles: list[tuple[int, str, str, bool]] = []
    for case_id in range(1, 5):
        roles.extend([
            (case_id, "bastankhah", "slsqp", False),
            (case_id, "bastankhah", "slsqp", True),
            (case_id, "bastankhah", "alpso", False),
        ])
    roles.append((2, "bastankhah", "alpso", True))
    roles.extend([
        (2, "jensen", "slsqp", False),
        (2, "jensen", "slsqp", True),
        (2, "jensen", "alpso", False),
    ])
    require(len(roles) == 16, "T84 final paper role count must be 16")
    receipts: list[dict] = []
    for case_id, wake, optimizer, use_wec in roles:
        common = [
            "--data", args.data, "--case", str(case_id),
            "--start-index", "0", "--wake", wake,
            "--optimizer", optimizer, "--smoke",
            "--wec" if use_wec else "--no-wec",
        ]
        serial = execute(args.binary, [*common, "--workers", "1"])
        parallel = execute(args.binary, [*common, "--workers", "20"])
        label = f"case{case_id}/{wake}/{optimizer}/wec={use_wec}"
        require(serial["scientific_hash"] == parallel["scientific_hash"],
                f"{label}: one/all-core scientific hash mismatch")
        require(serial["final_assessment"]["aep_gwh"]
                == parallel["final_assessment"]["aep_gwh"],
                f"{label}: one/all-core final AEP mismatch")
        require(parallel["observed_workers"] > 1,
                f"{label}: no multicore participation")
        require(parallel["final_assessment"]["maximum_constraint_violation_m"]
                <= 1.0e-3,
                f"{label}: final layout is infeasible")
        expected_stages = 1
        if optimizer == "slsqp" and wake == "bastankhah":
            expected_stages = 7 if use_wec else 2
        elif optimizer == "slsqp" and wake == "jensen":
            expected_stages = 6 if use_wec else 1
        elif optimizer == "alpso" and use_wec:
            expected_stages = 7
        require(len(parallel["stages"]) == expected_stages,
                f"{label}: lifecycle stage count mismatch")
        if optimizer == "alpso":
            expected_calls = 420 if use_wec else 60
            require(parallel["executed_function_calls"] == expected_calls,
                    f"{label}: smoke ALPSO call accounting mismatch")
            expected_budget = 26460 if use_wec else {
                1: 20130, 2: 21030, 3: 20280, 4: 20430,
            }[case_id]
            require(parallel["paper_function_call_budget"] == expected_budget,
                    f"{label}: paper ALPSO call budget mismatch")
        receipts.append({
            "role": label,
            "method_semantic_id": parallel["method_semantic_id"],
            "stages": len(parallel["stages"]),
            "observed_workers": parallel["observed_workers"],
            "scientific_hash": parallel["scientific_hash"],
        })

    print(json.dumps({
        "status": "pass",
        "problem_semantic_id": "t84_wec_four_case_author_data_v1",
        "evaluators": evaluations,
        "paper_final_roles": receipts,
        "role_count": len(receipts),
        "claim_boundary": (
            "source-backed flexible academic reproduction; not author "
            "SNOPT/Tapenade/PlantEnergy/random-state numerical replay"
        ),
    }, sort_keys=True))


if __name__ == "__main__":
    main()
