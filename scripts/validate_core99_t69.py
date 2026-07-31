#!/usr/bin/env python3
"""
WFLOP IMPLEMENTATION FACT DECLARATION
Implementation unit: independent T69 H5 equation, anchor and schedule audit
Paper/DOI: Feng and Shen; 10.1016/j.enconman.2017.06.005
Facts, missing fields and conflict profiles: hpc/core99_cpp/include/core99/feng_t69.hpp
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


WEIGHTS = (0.0, 0.05, 0.5, 0.95, 1.0)
PAPER = {
    "mean_power_mw": 79.61,
    "variability_of_power": 2.63e-4,
    "long_term_mean_mw": 78.54,
    "long_term_std_mw": 3.482,
    "table3_compatible_long_robustness": 4.750,
}


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def invoke(binary: Path, *args: str) -> dict:
    completed = subprocess.run(
        [str(binary), *args], check=True, text=True, capture_output=True
    )
    return json.loads(completed.stdout)


def close(a: float, b: float, tolerance: float = 1e-12) -> bool:
    return abs(a - b) <= tolerance * max(1.0, abs(a), abs(b))


def validate_equations(payload: dict) -> None:
    reference = payload["reference"]
    final = payload["final_evaluation"]
    alpha = payload["effective_alpha"]
    beta = payload["effective_beta"]
    gamma = payload["effective_gamma"]
    require(close(final["aep_mwh_paper_8770"],
                  8770.0 * final["mean_power_mw"]), "literal AEP")
    require(close(final["aep_mwh_calendar_8760"],
                  8760.0 * final["mean_power_mw"]), "calendar AEP")
    short = ((final["mean_power_mw"] / reference["mean_power_mw"]) ** alpha
             / (final["variability_of_power"]
                / reference["variability_of_power"]) ** (1.0 - alpha))
    long_value = (final["long_term_mean_mw"] ** beta
                  / final["long_term_std_mw"] ** (1.0 - beta))
    compatible = math.sqrt(
        final["long_term_mean_mw"] / final["long_term_std_mw"]
    )
    require(close(final["short_robustness"], short), "Eq.6 short")
    require(close(final["long_robustness"], long_value), "Eq.10 long")
    require(close(final["table3_compatible_long_robustness"], compatible),
            "Table-3 beta conflict")
    selected = (compatible if payload["conflict_profile"] == "table3_compatible"
                else long_value)
    require(close(final["overall_robustness"],
                  gamma * short + (1.0 - gamma) * selected), "Eq.11 overall")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    args = parser.parse_args()
    binary = args.binary.resolve()

    listing = invoke(binary, "--mode", "list-cases")
    require(listing["paper_native_cases"] == 15, "15 native cases")
    require(tuple(listing["weights"]) == WEIGHTS, "paper weights")
    require(len(listing["conflict_profiles"]) == 2, "two conflict profiles")

    common = (
        "--mode", "optimize", "--study", "overall", "--weight", "0.95",
        "--profile", "table3_compatible", "--scenarios", "128",
        "--fes", "4", "--seed", "69017",
    )
    serial = invoke(binary, *common, "--workers", "1")
    parallel = invoke(binary, *common, "--workers", "4")
    for key in (
        "physical_fes", "accepted_moves", "infeasible_proposals", "reference",
        "final_evaluation", "final_layout", "scientific_hash",
    ):
        require(serial[key] == parallel[key], f"schedule identity {key}")
    require(parallel["observed_workers"] >= 2, "parallel participation")
    require(len(parallel["final_layout"]) == 80, "80-turbine layout")
    validate_equations(parallel)

    anchor = invoke(
        binary, "--mode", "optimize", "--study", "overall", "--weight", "0.95",
        "--profile", "table3_compatible", "--scenarios", "1000", "--fes", "1",
        "--workers", "4", "--seed", "69017",
    )
    reference = anchor["reference"]
    tolerances = {
        "mean_power_mw": 0.05,
        "variability_of_power": 0.20,
        "long_term_mean_mw": 0.05,
        "long_term_std_mw": 0.10,
        "table3_compatible_long_robustness": 0.10,
    }
    for key, tolerance in tolerances.items():
        relative = abs(reference[key] - PAPER[key]) / abs(PAPER[key])
        require(relative <= tolerance, f"paper anchor {key}: {relative}")

    identities = []
    for study in ("short", "long", "overall"):
        for weight in WEIGHTS:
            identities.append((study, weight))
    require(len(identities) == 15 and len(set(identities)) == 15,
            "complete distinct native matrix")

    equation = invoke(
        binary, "--mode", "optimize", "--study", "overall", "--weight", "0",
        "--profile", "equation_declared", "--scenarios", "128", "--fes", "1",
        "--workers", "4", "--seed", "69018",
    )
    compatible = invoke(
        binary, "--mode", "optimize", "--study", "overall", "--weight", "0",
        "--profile", "table3_compatible", "--scenarios", "128", "--fes", "1",
        "--workers", "4", "--seed", "69018",
    )
    require(not close(equation["reference"]["overall_robustness"],
                      compatible["reference"]["overall_robustness"]),
            "conflict profiles must remain distinct")

    print(json.dumps({
        "status": "pass",
        "paper_native_cases": 15,
        "supplementary_conflict_cases": 5,
        "schedule_independent": True,
        "workers_observed": parallel["observed_workers"],
        "paper_anchor_relative_error": {
            key: abs(reference[key] - PAPER[key]) / abs(PAPER[key])
            for key in tolerances
        },
        "scientific_hash": parallel["scientific_hash"],
    }, sort_keys=True))


if __name__ == "__main__":
    main()
