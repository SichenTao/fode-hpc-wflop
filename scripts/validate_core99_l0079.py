#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: independent L0079 H5 role, anchor and identity audit
Paper/DOI: Pillai et al.; 10.1016/j.oceaneng.2017.04.049
Facts, omissions and completions: hpc/core99_cpp/include/core99/pillai_l0079.hpp
Claim boundary: validates flexible reconstruction, not author numeric replay
Last evidence-audit date: 2026-07-31
END WFLOP IMPLEMENTATION FACT DECLARATION
"""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import subprocess


ROLES = (
    ("ga", "array"),
    ("ga", "binary"),
    ("ga", "continuous"),
    ("pso", "array"),
    ("pso", "binary"),
    ("pso", "continuous"),
)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def invoke(binary: Path, *args: str) -> dict:
    completed = subprocess.run(
        [str(binary), *args], check=True, text=True, capture_output=True
    )
    return json.loads(completed.stdout)


def close(first: float, second: float, tolerance: float = 1e-11) -> bool:
    return abs(first - second) <= tolerance * max(1.0, abs(first), abs(second))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    args = parser.parse_args()
    binary = args.binary.resolve()

    listing = invoke(binary, "--action", "list-cases")
    require(len(listing["paper_native_roles"]) == 6, "six native roles")
    require(listing["native_repeats"] == 1, "single native run")
    require(listing["population_or_swarm"] == 100, "paper population")
    require(listing["maximum_generations"] == 1000, "paper generations")
    require(listing["journal_candidates"] == 628, "journal candidates")
    require(listing["thesis_candidates"] == 658, "thesis candidates")

    reference = invoke(binary, "--action", "evaluate-reference")
    evaluation = reference["evaluation"]
    require(reference["candidate_count"] == 628, "default target profile")
    require(len(reference["layout"]) == 20, "twenty as-built turbines")
    require(evaluation["feasible"], "as-built feasibility")
    require(close(evaluation["net_aep_mwh_8766"], 95410.0 / 0.93),
            "as-built AEP calibration")
    require(close(evaluation["net_aep_mwh_8760"],
                  evaluation["net_aep_mwh_8766"] * 8760.0 / 8766.0),
            "8766/8760 identity")
    require(close(evaluation["lifetime_cost_gbp"], 91_500_000.0),
            "lifetime cost anchor")
    require(close(evaluation["lcoe_gbp_per_mwh"], 86.63), "LCOE anchor")

    role_receipts = []
    for role_index, (optimizer, mode) in enumerate(ROLES):
        payload = invoke(
            binary,
            "--action", "optimize",
            "--optimizer", optimizer,
            "--constraint-mode", mode,
            "--candidate-profile", "journal_628",
            "--population", "20",
            "--maximum-generations", "1",
            "--disable-convergence",
            "--workers", "4",
            "--seed", str(79030 + role_index),
        )
        require(len(payload["best_layout"]) == 20, f"{optimizer}/{mode} layout")
        require(payload["best_evaluation"]["feasible"], f"{optimizer}/{mode} feasible")
        require(payload["best_evaluation"]["minimum_spacing_m"] >= 175.0 - 1e-8,
                f"{optimizer}/{mode} spacing")
        require(payload["observed_workers"] >= 2, f"{optimizer}/{mode} parallel")
        expected_fes = 52 if optimizer == "ga" else 40
        require(payload["physical_fes"] == expected_fes,
                f"{optimizer}/{mode} physical FES")
        role_receipts.append(payload["case_id"])

    common = (
        "--action", "optimize", "--optimizer", "pso",
        "--constraint-mode", "continuous", "--population", "20",
        "--maximum-generations", "2", "--disable-convergence",
        "--seed", "79079",
    )
    serial = invoke(binary, *common, "--workers", "1")
    parallel = invoke(binary, *common, "--workers", "4")
    for key in (
        "generations", "physical_fes", "convergence_reason", "best_evaluation",
        "best_layout", "scientific_hash",
    ):
        require(serial[key] == parallel[key], f"schedule identity {key}")

    thesis = invoke(
        binary, "--action", "evaluate-reference",
        "--candidate-profile", "thesis_658",
    )
    require(thesis["candidate_count"] == 658, "separate thesis profile")
    print(json.dumps({
        "status": "pass",
        "paper_native_roles": role_receipts,
        "journal_candidates": 628,
        "thesis_candidates": 658,
        "schedule_independent": True,
        "parallel_workers_observed": parallel["observed_workers"],
        "scientific_hash": parallel["scientific_hash"],
    }, sort_keys=True))


if __name__ == "__main__":
    main()
